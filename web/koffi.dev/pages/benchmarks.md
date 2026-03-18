# Overview

Here is a quick overview of the execution time of Koffi calls on three benchmarks, where it is compared to a theoretical ideal FFI implementation (approximated with pre-compiled static N-API glue code):

- The first benchmark is based on `rand()` calls
- The second benchmark is based on `atoi()` calls
- The third benchmark is based on [Raylib](https://www.raylib.com/)

<p style="text-align: center;">
    <a href="{{ ASSET static/perf_linux.png }}" target="_blank"><img src="{{ ASSET static/perf_linux.png }}" alt="Linux x86_64 performance" style="width: 350px;"/></a>
    <a href="{{ ASSET static/perf_windows.png }}" target="_blank"><img src="{{ ASSET static/perf_windows.png }}" alt="Windows x86_64 performance" style="width: 350px;"/></a>
</p>

These results are detailed and explained below, and compared to node-ffi/node-ffi-napi.

# Linux x86_64

The results presented below were measured on my x86_64 Linux machine (Intel® Core™ Ultra 9 185H).

## rand results

This test is based around repeated calls to a simple standard C function `rand`, and has three implementations:

- the first one is the reference, it calls rand through an N-API module, and is close to the theoretical limit of a perfect (no overhead) Node.js > C FFI implementation (pre-compiled static glue code)
- the second one calls rand through Koffi
- the third one uses the official Node.js FFI implementation, node-ffi-napi

rand          | Iteration time | Relative performance | Overhead
------------- | -------------- | -------------------- | --------
napi          | 256 ns         | x1.00                | +0%
koffi         | 375 ns         | x0.68                | +46%
node-ffi-napi | 29783 ns       | x0.009               | +11544%

Because rand is a pretty small function, the FFI overhead is clearly visible.

## atoi results

This test is similar to the rand one, but it is based on `atoi`, which takes a string parameter. Javascript (V8) to C string conversion is relatively slow and heavy.

atoi          | Iteration time | Relative performance | Overhead
------------- | -------------- | -------------------- | --------
napi          | 371 ns         | x1.00                | +0%
koffi         | 557 ns         | x0.67                | +50%
node-ffi-napi | 104340 ns      | x0.004               | +27988%

Because atoi is a pretty small function, the FFI overhead is clearly visible.

## Raylib results

This benchmark uses the CPU-based image drawing functions in Raylib. The calls are much heavier than in previous benchmarks, thus the FFI overhead is reduced. In this implementation, Koffi is compared to:

- Baseline: Full C++ version of the code (no JS)
- [node-raylib](https://github.com/RobLoach/node-raylib): This is a native wrapper implemented with N-API

raylib        | Iteration time | Relative performance | Overhead
------------- | -------------- | -------------------- | --------
cc            | 10.8 µs        | x1.14                | -12%
napi          | 12.3 µs        | x1.00                | +0%
koffi         | 13.2 µs        | x0.92                | +8%
node-ffi-napi | 80.3 µs        | x0.15                | +555%

# Windows x86_64

The results presented below were measured on my x86_64 Windows machine (Intel® Core™ i5-4460).

## rand results

This test is based around repeated calls to a simple standard C function `rand`, and has three implementations:

- the first one is the reference, it calls rand through an N-API module, and is close to the theoretical limit of a perfect (no overhead) Node.js > C FFI implementation (pre-compiled static glue code)
- the second one calls rand through Koffi
- the third one uses the official Node.js FFI implementation, node-ffi-napi

rand          | Iteration time | Relative performance | Overhead
------------- | -------------- | -------------------- | --------
napi          | 859 ns         | x1.00                | (ref)
koffi         | 1352 ns        | x0.64                | +57%
node-ffi-napi | 35640 ns       | x0.02                | +4048%

Because rand is a pretty small function, the FFI overhead is clearly visible.

## atoi results

This test is similar to the rand one, but it is based on `atoi`, which takes a string parameter. Javascript (V8) to C string conversion is relatively slow and heavy.

The results below were measured on my x86_64 Windows machine (Intel® Core™ i5-4460):

atoi          | Iteration time | Relative performance | Overhead
------------- | -------------- | -------------------- | --------
napi          | 1336 ns        | x1.00                | (ref)
koffi         | 2440 ns        | x0.55                | +83%
node-ffi-napi | 136890 ns      | x0.010               | +10144%

Because atoi is a pretty small function, the FFI overhead is clearly visible.

## Raylib results

This benchmark uses the CPU-based image drawing functions in Raylib. The calls are much heavier than in the atoi benchmark, thus the FFI overhead is reduced. In this implementation, Koffi is compared to:

- [node-raylib](https://github.com/RobLoach/node-raylib) (baseline): This is a native wrapper implemented with N-API
- raylib_cc: C++ implementation of the benchmark, without any Javascript

raylib        | Iteration time | Relative performance | Overhead
------------- | -------------- | -------------------- | --------
cc            | 18.2 µs        | x1.50                | -33%
napi          | 27.3 µs        | x1.00                | (ref)
koffi         | 29.8 µs        | x0.92                | +9%
node-ffi-napi | 96.3 µs        | x0.28                | +253%

# Running benchmarks

Please note that all benchmark results on this page are made with Clang-built binaries.

```sh
cd src/koffi
node ../cnoke/cnoke.js --clang --release

cd benchmark
node ../../cnoke/cnoke.js --clang --release
```

Once everything is built and ready, run:

```sh
node benchmark.js
```
