import uvicorn
from fastapi import FastAPI, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse

from Src.Core.constans import API_PREFIX, APP_NAME, APP_VERSION, DEFAULT_HOST, DEFAULT_PORT
from Src.Core.validator import ConfigValidationError
from Src.Core.responces import ErrorResponse
from Src.Routers.config_router import router as config_router
from Src.Routers.pipeline_router import router as pipeline_router
from Src.Routers.section_router import router as section_router
from Src.Routers.dashboard_router import router as dashboard_router
app = FastAPI(
    title=APP_NAME,
    version=APP_VERSION,
)


@app.exception_handler(ConfigValidationError)
async def config_validation_exception_handler(request: Request, exc: ConfigValidationError):
    return JSONResponse(
        status_code=400,
        content=exc.to_dict(),
    )


@app.exception_handler(RequestValidationError)
async def request_validation_exception_handler(request: Request, exc: RequestValidationError):
    return JSONResponse(
        status_code=422,
        content=ErrorResponse(
            ok=False,
            error_type="REQUEST_VALIDATION_ERROR",
            error_code=4000,
            message="Invalid request payload",
            details={"errors": exc.errors()},
        ).model_dump(),
    )


@app.exception_handler(Exception)
async def generic_exception_handler(request: Request, exc: Exception):
    return JSONResponse(
        status_code=500,
        content=ErrorResponse(
            ok=False,
            error_type="INTERNAL_SERVER_ERROR",
            error_code=5000,
            message=str(exc),
            details={},
        ).model_dump(),
    )


app.include_router(section_router, prefix=API_PREFIX)
app.include_router(config_router, prefix=API_PREFIX)
app.include_router(pipeline_router, prefix=API_PREFIX)
app.include_router(dashboard_router)

@app.get("/")
def root():
    return {
        "message": "Conveyor API",
        "docs": "/docs",
        "openapi": "/openapi.json",
    }


if __name__ == "__main__":
    import uvicorn

    uvicorn.run("main:app", host="0.0.0.0", port=8080, reload=True)