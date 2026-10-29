# wasm3 (vendored)

[wasm3](https://github.com/wasm3/wasm3) v0.5.0, commit 6b8bcb1e07bf26ebef09a7211b0a37a446eafd52, MIT ([LICENSE](LICENSE)):
`source/`'s `.c` and `.h` files unchanged (its CMakeLists.txt, `extensions/` and `extra/` left out). The interpreter
RemoteFM1 for iOS runs .rfm1 files in; here tools/rfm1_run uses it to boot and play them (cmake/FM1Rfm1.cmake builds
the twelve units `m3_{bind,code,compile,core,emit,env,exec,function,info,module,optimize,parse}.c`).
