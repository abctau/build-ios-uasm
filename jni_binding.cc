#include <jni.h>

extern "C" JNIEXPORT jdouble JNICALL
Java_uts_sdk_modules_testUasm_TestUasm_nativeAdd(
    JNIEnv *, jobject, jdouble a, jdouble b) {
    return a + b;
}
