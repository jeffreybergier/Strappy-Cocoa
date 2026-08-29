/* Capture raw exports before Emscripten applies Asyncify wrappers. */
{
  const strappyInstantiateWasm = Module.instantiateWasm;
  Module.instantiateWasm = function(imports, onSuccess) {
    return strappyInstantiateWasm(imports, (instance, wasmModule) => {
      Module.strappyRawWasmExports = instance.exports;
      onSuccess(instance, wasmModule);
    });
  };
}

