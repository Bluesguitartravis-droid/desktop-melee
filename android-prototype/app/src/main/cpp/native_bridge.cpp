#include <jni.h>
#include <string>
extern "C" JNIEXPORT jstring JNICALL
Java_com_desktopmelee_android_MainActivity_nativeBridgeStatus(JNIEnv* env, jclass) {
    const std::string status = "loaded (C++20 / JNI)";
    return env->NewStringUTF(status.c_str());
}
