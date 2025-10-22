// portal_emulator.cpp - Simplified to just slot management
#include <jni.h>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>

#define LOG_TAG "PortalEmulator"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define MAX_SLOTS 2
#define PORTAL_BUFFER_SIZE 1024

struct PortalSlot {
    uint8_t data[PORTAL_BUFFER_SIZE];
    size_t size;
    bool present;
    bool loaded;
};

static PortalSlot g_slots[MAX_SLOTS];

// Keep only these functions - no threading, no emulator
extern "C" JNIEXPORT jint JNICALL
Java_com_kaos_portalemulator_MainActivity_nativeInit(JNIEnv*, jobject) {
    LOGI("Native init called");
    memset(g_slots, 0, sizeof(g_slots));
    return 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_kaos_portalemulator_MainActivity_nativeSetSlotFile(
        JNIEnv* env, jobject, jint slot, jstring path) {

    if (slot < 0 || slot >= MAX_SLOTS) return -1;

    const char* path_str = env->GetStringUTFChars(path, nullptr);
    LOGI("Loading file into slot %d: %s", slot, path_str);

    int fd = open(path_str, O_RDONLY);
    env->ReleaseStringUTFChars(path, path_str);

    if (fd < 0) return -1;

    ssize_t bytes_read = read(fd, g_slots[slot].data, PORTAL_BUFFER_SIZE);
    close(fd);

    if (bytes_read < 0) return -1;

    g_slots[slot].size = bytes_read;
    return 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_kaos_portalemulator_MainActivity_nativeLoadSlot(
        JNIEnv*, jobject, jint slot) {
    if (slot < 0 || slot >= MAX_SLOTS) return -1;
    g_slots[slot].present = true;
    g_slots[slot].loaded = true;
    return 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_kaos_portalemulator_MainActivity_nativeUnloadSlot(
        JNIEnv*, jobject, jint slot) {
    if (slot < 0 || slot >= MAX_SLOTS) return -1;
    g_slots[slot].present = false;
    g_slots[slot].loaded = false;
    return 0;
}

// Remove all the nativeStartEmulator, nativeAreEndpointsReady, etc.
// The daemon will handle that