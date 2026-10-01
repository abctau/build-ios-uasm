package uts.sdk.modules.testUasm

object TestUasm {
    init {
        // 不需要加载编译出的so库，当用户调用 uni.loadUasm()/uni.loadUasmSync() 时自动加载
        // 在开发期间 .so 库的位置不在android默认的lib目录下
        // System.loadLibrary("UasmTestUasm")
    }

    fun add(a: Double, b: Double): Double = nativeAdd(a, b)

    private external fun nativeAdd(a: Double, b: Double): Double
}
