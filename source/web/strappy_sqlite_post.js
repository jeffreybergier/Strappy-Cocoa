/* Preserve the Emscripten call bridge on SQLite's public namespace. */
{
  const strappySQLitePostLoadInit = Module.runSQLite3PostLoadInit;
  Module.runSQLite3PostLoadInit = async function(...args) {
    if (Module.strappyRawWasmExports) {
      for (const [name, rawExport] of Object.entries(
        Module.strappyRawWasmExports,
      )) {
        const asyncifyExport = Module[`_${name}`];
        if (
          typeof rawExport === "function" &&
          typeof asyncifyExport === "function" &&
          asyncifyExport.length !== rawExport.length
        ) {
          Object.defineProperty(asyncifyExport, "length", {
            configurable: true,
            value: rawExport.length,
          });
        }
      }
      const strappyNodeConfig = typeof process === "object" && process
        ? { disable: { vfs: { opfs: true, "opfs-wl": true } } }
        : {};
      globalThis.sqlite3ApiConfig = Object.assign(
        Object.create(null),
        globalThis.sqlite3ApiConfig || {},
        strappyNodeConfig,
        { exports: Module.strappyRawWasmExports },
      );
      if (globalThis.sqlite3ApiBootstrap?.defaultConfig) {
        globalThis.sqlite3ApiBootstrap.defaultConfig.exports =
          Module.strappyRawWasmExports;
      }
      const sqlite3ApiBootstrap = globalThis.sqlite3ApiBootstrap;
      if (typeof sqlite3ApiBootstrap === "function") {
        const strappySqlite3ApiBootstrap = function(config) {
          return sqlite3ApiBootstrap(Object.assign(
            Object.create(null),
            config || {},
            { exports: Module.strappyRawWasmExports },
          ));
        };
        Object.assign(strappySqlite3ApiBootstrap, sqlite3ApiBootstrap);
        globalThis.sqlite3ApiBootstrap = strappySqlite3ApiBootstrap;
      }
    }
    const sqlite3 = await strappySQLitePostLoadInit.apply(Module, args);
    delete Module.strappyRawWasmExports;
    sqlite3.ccall = Module.ccall.bind(Module);
    sqlite3.FS = Module.FS;
    return sqlite3;
  };
}
