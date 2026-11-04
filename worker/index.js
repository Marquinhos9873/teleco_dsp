// Worker unificado (reemplaza las Pages Functions del documento original).
// Paso 1 (esqueleto + gemelo): sin rutas de API todavía, todo se sirve como asset estático.
// Las rutas /api/d/* y /api/* se agregan acá a medida que se implementan (pasos 3 a 6).
export default {
  async fetch(request, env) {
    return env.ASSETS.fetch(request);
  },
};
