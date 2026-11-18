"""
SombraViva — Exemplo do endpoint que recebe as medições do ESP32 (Wokwi).

Integração SOA + IoT + Cyber:
  - POST /api/v1/medicoes  -> recebe a medição de UMA zona (o ESP32 manda 2 por ciclo)
  - Autenticação do dispositivo por token Bearer (diferente do JWT dos usuários)
  - Tentativas com token inválido são registradas no log de auditoria

Para testar isolado:
    pip install fastapi uvicorn
    export SOMBRAVIVA_DEVICE_TOKENS="esp32-osasco-calcadao-01:sv-dev-TROQUE-ESTE-TOKEN"
    uvicorn api_medicoes_exemplo:app --reload --port 8000
    ngrok http 8000   # gera a URL pública para colocar em API_URL no sketch.ino

No projeto real, troque a lista em memória por um INSERT no MySQL
(SQLAlchemy/repositório que vocês já usam no backend de SOA).
"""

import hmac
import logging
import os
from datetime import datetime, timezone, timedelta
from enum import Enum
from typing import Optional

from fastapi import APIRouter, Depends, FastAPI, Header, HTTPException, Request, status
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from pydantic import BaseModel, Field

FUSO_BRASILIA = timezone(timedelta(hours=-3))

# Log de auditoria de segurança (pode ir para tabela no MySQL / painel de auditoria)
audit_log = logging.getLogger("sombraviva.auditoria")
logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(levelname)s %(message)s")


# ---------------------------------------------------------------------------
# Tokens dos dispositivos: "device_id:token,device_id2:token2"
# Nunca versionar tokens reais no GitHub — use variável de ambiente / .env
# ---------------------------------------------------------------------------
def carregar_tokens() -> dict[str, str]:
    bruto = os.getenv("SOMBRAVIVA_DEVICE_TOKENS", "esp32-osasco-calcadao-01:sv-dev-TROQUE-ESTE-TOKEN")
    pares = (item.split(":", 1) for item in bruto.split(",") if ":" in item)
    return {device.strip(): token.strip() for device, token in pares}


DEVICE_TOKENS = carregar_tokens()
bearer = HTTPBearer(auto_error=False)


def autenticar_dispositivo(
    request: Request,
    credenciais: Optional[HTTPAuthorizationCredentials] = Depends(bearer),
    x_device_id: Optional[str] = Header(default=None),
) -> str:
    """Valida o token Bearer do ESP32. Retorna o device_id autenticado."""
    ip = request.client.host if request.client else "desconhecido"
    token_esperado = DEVICE_TOKENS.get(x_device_id or "")
    if (
        credenciais is None
        or token_esperado is None
        or not hmac.compare_digest(credenciais.credentials, token_esperado)  # evita timing attack
    ):
        audit_log.warning("ACESSO_NEGADO dispositivo=%s ip=%s rota=%s", x_device_id, ip, request.url.path)
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Dispositivo não autorizado",
            headers={"WWW-Authenticate": "Bearer"},
        )
    return x_device_id


# ---------------------------------------------------------------------------
# Modelos Pydantic (mesmos campos do JSON enviado pelo sketch.ino)
# ---------------------------------------------------------------------------
class Zona(str, Enum):
    arborizada = "arborizada"
    sem_vegetacao = "sem_vegetacao"


class NivelRisco(str, Enum):
    normal = "normal"
    atencao = "atencao"
    alerta = "alerta"
    perigo = "perigo"


class MedicaoIn(BaseModel):
    device_id: str = Field(..., max_length=64, examples=["esp32-osasco-calcadao-01"])
    zona: Zona
    temperatura: float = Field(..., ge=-40, le=80, description="°C (faixa do DHT22)")
    umidade: float = Field(..., ge=0, le=100, description="% UR")
    indice_calor: float = Field(..., description="Sensação térmica em °C (NOAA)")
    luminosidade: float = Field(..., ge=0, description="Incidência solar em lux (LDR)")
    nivel_risco: NivelRisco
    delta_ilha_calor: Optional[float] = Field(None, description="Calçadão - Arborizada, em °C")
    temperatura_oficial: Optional[float] = Field(None, description="Open-Meteo Osasco, °C")
    timestamp: Optional[datetime] = Field(None, description="ISO 8601; se nulo, usa hora do servidor")


class MedicaoOut(MedicaoIn):
    id: int
    recebido_em: datetime


# ---------------------------------------------------------------------------
# Rotas
# ---------------------------------------------------------------------------
router = APIRouter(prefix="/api/v1", tags=["Medições IoT"])
_medicoes: list[MedicaoOut] = []  # substituir por repositório MySQL


@router.post("/medicoes", response_model=MedicaoOut, status_code=status.HTTP_201_CREATED)
def criar_medicao(medicao: MedicaoIn, device_autenticado: str = Depends(autenticar_dispositivo)):
    # Um dispositivo só pode publicar em seu próprio nome
    if medicao.device_id != device_autenticado:
        audit_log.warning("DEVICE_ID_DIVERGENTE token=%s corpo=%s", device_autenticado, medicao.device_id)
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="device_id não confere com o token")

    agora = datetime.now(FUSO_BRASILIA)
    nova = MedicaoOut(
        **medicao.model_dump(exclude={"timestamp"}),
        timestamp=medicao.timestamp or agora,
        id=len(_medicoes) + 1,
        recebido_em=agora,
    )
    # Exemplo MySQL (SQLAlchemy):
    #   db.add(MedicaoModel(**nova.model_dump(exclude={"id"}))); db.commit()
    _medicoes.append(nova)
    if nova.nivel_risco in (NivelRisco.alerta, NivelRisco.perigo):
        audit_log.info("ALERTA_CALOR zona=%s indice=%.1f risco=%s", nova.zona.value, nova.indice_calor, nova.nivel_risco.value)
    return nova


@router.get("/medicoes", response_model=list[MedicaoOut])
def listar_medicoes(zona: Optional[Zona] = None, limite: int = 100):
    """Usado pelo dashboard do site para os gráficos Arborizada x Calçadão."""
    dados = [m for m in _medicoes if zona is None or m.zona == zona]
    return dados[-limite:]


app = FastAPI(title="SombraViva API — módulo IoT", version="1.0.0")
app.include_router(router)
