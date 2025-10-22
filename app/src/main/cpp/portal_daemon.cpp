// portal_daemon.cpp - Runs with root privileges
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <linux/usb/functionfs.h>
#include <linux/usb/ch9.h>
#include <endian.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include "skylander_crypto.h"

#define MAX_SLOTS 2
#define PORTAL_BUFFER_SIZE 1024

// Logging macros for standalone executable (uses printf instead of Android log)
#define LOGI(...) do { printf("[INFO] "); printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while(0)
#define LOGE(...) do { fprintf(stderr, "[ERROR] "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); fflush(stderr); } while(0)

#define MAX_SLOTS 2
#define PORTAL_BUFFER_SIZE 1024

// Endianness conversion (same as your original)
#ifndef htole32
#define htole32(x) (x)
#endif
#ifndef htole16
#define htole16(x) (x)
#endif

static const uint8_t hid_report_descriptor[] = {
        0x06, 0x00, 0xFF,
        0x09, 0x01,
        0xA1, 0x01,
        0x19, 0x01,
        0x29, 0x40,
        0x15, 0x00,
        0x26, 0xFF, 0x00,
        0x75, 0x08,
        0x95, 0x20,
        0x81, 0x00,
        0x19, 0x01,
        0x29, 0xFF,
        0x91, 0x00,
        0xC0
};

// Portal state
struct PortalSlot {
    uint8_t data[PORTAL_BUFFER_SIZE];
    size_t size;
    bool present;
    bool loaded;
};

struct PortalState {
    PortalSlot slots[MAX_SLOTS];
    bool running;
    int ep0_fd;
    int ep_in_fd;
    int ep_out_fd;
};

static PortalState g_portal;

static int write_descriptors(int fd) {
    LOGI("Writing USB descriptors...");

    struct {
        struct usb_functionfs_descs_head_v2 header;
        __le32 fs_count;
        __le32 hs_count;

        // FS descriptors
        struct usb_interface_descriptor fs_intf;
        struct {
            __u8 bLength;
            __u8 bDescriptorType;
            __le16 bcdHID;
            __u8 bCountryCode;
            __u8 bNumDescriptors;
            __u8 bDescriptorType2;
            __le16 wDescriptorLength;
        } __attribute__((packed)) fs_hid;
        struct usb_endpoint_descriptor_no_audio fs_ep_in;
        struct usb_endpoint_descriptor_no_audio fs_ep_out;

        // HS descriptors
        struct usb_interface_descriptor hs_intf;
        struct {
            __u8 bLength;
            __u8 bDescriptorType;
            __le16 bcdHID;
            __u8 bCountryCode;
            __u8 bNumDescriptors;
            __u8 bDescriptorType2;
            __le16 wDescriptorLength;
        } __attribute__((packed)) hs_hid;
        struct usb_endpoint_descriptor_no_audio hs_ep_in;
        struct usb_endpoint_descriptor_no_audio hs_ep_out;
    } __attribute__((packed)) descs = {
            .header = {
                    .magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
                    .length = htole32(sizeof(descs)),
                    .flags = htole32(FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC),
            },
            .fs_count = htole32(4),
            .hs_count = htole32(4),

            // FS Interface
            .fs_intf = {
                    .bLength = 9,
                    .bDescriptorType = USB_DT_INTERFACE,
                    .bInterfaceNumber = 0,
                    .bAlternateSetting = 0,
                    .bNumEndpoints = 2,
                    .bInterfaceClass = 0x03,    // HID
                    .bInterfaceSubClass = 0x00,
                    .bInterfaceProtocol = 0x00,
                    .iInterface = 0,
            },

            // FS HID Descriptor
            .fs_hid = {
                    .bLength = 9,
                    .bDescriptorType = 0x21,
                    .bcdHID = htole16(0x0111),
                    .bCountryCode = 0x00,
                    .bNumDescriptors = 1,
                    .bDescriptorType2 = 0x22,
                    .wDescriptorLength = htole16(sizeof(hid_report_descriptor)),  // 29 bytes
            },

            // FS IN Endpoint
            .fs_ep_in = {
                    .bLength = 7,
                    .bDescriptorType = USB_DT_ENDPOINT,
                    .bEndpointAddress = 0x81,
                    .bmAttributes = 0x03,
                    .wMaxPacketSize = htole16(0x0040),  // 64 bytes
                    .bInterval = 0x01,
            },

            // FS OUT Endpoint
            .fs_ep_out = {
                    .bLength = 7,
                    .bDescriptorType = USB_DT_ENDPOINT,
                    .bEndpointAddress = 0x02,
                    .bmAttributes = 0x03,
                    .wMaxPacketSize = htole16(0x0040),  // 64 bytes
                    .bInterval = 0x01,
            },

            // HS Interface
            .hs_intf = {
                    .bLength = 9,
                    .bDescriptorType = USB_DT_INTERFACE,
                    .bInterfaceNumber = 0,
                    .bAlternateSetting = 0,
                    .bNumEndpoints = 2,
                    .bInterfaceClass = 0x03,
                    .bInterfaceSubClass = 0x00,
                    .bInterfaceProtocol = 0x00,
                    .iInterface = 0,
            },

            .hs_hid = {
                    .bLength = 9,
                    .bDescriptorType = 0x21,
                    .bcdHID = htole16(0x0111),
                    .bCountryCode = 0x00,
                    .bNumDescriptors = 1,
                    .bDescriptorType2 = 0x22,
                    .wDescriptorLength = htole16(sizeof(hid_report_descriptor)),
            },

            // HS Endpoints
            .hs_ep_in = {
                    .bLength = 7,
                    .bDescriptorType = USB_DT_ENDPOINT,
                    .bEndpointAddress = 0x81,
                    .bmAttributes = 0x03,
                    .wMaxPacketSize = htole16(0x0040),
                    .bInterval = 0x01,
            },

            .hs_ep_out = {
                    .bLength = 7,
                    .bDescriptorType = USB_DT_ENDPOINT,
                    .bEndpointAddress = 0x02,
                    .bmAttributes = 0x03,
                    .wMaxPacketSize = htole16(0x0040),
                    .bInterval = 0x01,
            },
    };

    int ret = write(fd, &descs, sizeof(descs));
    if (ret < 0) {
        LOGE("Failed to write descriptors: %d (%s)", errno, strerror(errno));
        return -1;
    }

    LOGI("Descriptors written: %d bytes (expected %zu)", ret, sizeof(descs));

    // Log descriptor details
    LOGI("Interface: class=0x%02x subclass=0x%02x protocol=0x%02x",
         descs.fs_intf.bInterfaceClass,
         descs.fs_intf.bInterfaceSubClass,
         descs.fs_intf.bInterfaceProtocol);

    LOGI("EP IN: addr=0x%02x attr=0x%02x maxpkt=%d interval=%d",
         descs.fs_ep_in.bEndpointAddress,
         descs.fs_ep_in.bmAttributes,
         descs.fs_ep_in.wMaxPacketSize,
         descs.fs_ep_in.bInterval);

    LOGI("EP OUT: addr=0x%02x attr=0x%02x maxpkt=%d interval=%d",
         descs.fs_ep_out.bEndpointAddress,
         descs.fs_ep_out.bmAttributes,
         descs.fs_ep_out.wMaxPacketSize,
         descs.fs_ep_out.bInterval);

    // Write strings
    struct {
        struct usb_functionfs_strings_head header;
        __le16 lang;
        char manufacturer[12];
        char product[12];
        char serial[12];
    } __attribute__((packed)) strings = {
            .header = {
                    .magic = htole32(FUNCTIONFS_STRINGS_MAGIC),
                    .length = htole32(sizeof(strings)),
                    .str_count = htole32(3),
                    .lang_count = htole32(1),
            },
            .lang = htole16(0x0409),
            .manufacturer = "Activision",
            .product = "Spyro Porta",
            .serial = "99B3f9C9E6",
    };
    LOGI("sizeof(descs)=%zu, fs_count=%u, hs_count=%u, hid_len=%zu", sizeof(descs), descs.fs_count, descs.hs_count, sizeof(hid_report_descriptor));
    ret = write(fd, &strings, sizeof(strings));
    if (ret < 0) {
        LOGE("Failed to write strings: %d (%s)", errno, strerror(errno));
        return -1;
    }

    LOGI("Strings written: %d bytes", ret);
    LOGI("Manufacturer: '%s'", strings.manufacturer);
    LOGI("Product: '%s'", strings.product);
    LOGI("=== DESCRIPTORS COMPLETE ===");
    return 0;
}

static void handle_setup_request(const struct usb_ctrlrequest *setup) {
    LOGI("=== SETUP REQUEST ===");
    LOGI("bmRequestType=0x%02x bRequest=0x%02x wValue=0x%04x wIndex=0x%04x wLength=%d",
         setup->bRequestType, setup->bRequest, setup->wValue, setup->wIndex, setup->wLength);

    uint8_t response[256];
    memset(response, 0, sizeof(response));
    int response_len = -1;

    uint8_t request_type = setup->bRequestType & USB_TYPE_MASK;
    uint8_t direction = setup->bRequestType & USB_DIR_IN;

    if (request_type == USB_TYPE_STANDARD) {
        if (setup->bRequest == USB_REQ_GET_DESCRIPTOR) {
            uint8_t desc_type = (setup->wValue >> 8) & 0xFF;

            LOGI("GET_DESCRIPTOR: type=0x%02x", desc_type);
            if (desc_type == 0x21) {  // HID Descriptor
                LOGI("Sending HID Descriptor");
                // Construct HID descriptor
                uint8_t hid_desc[] = {
                        0x09,        // bLength
                        0x21,        // bDescriptorType (HID)
                        0x11, 0x01,  // bcdHID (1.11)
                        0x00,        // bCountryCode
                        0x01,        // bNumDescriptors
                        0x22,        // bDescriptorType (Report)
                        sizeof(hid_report_descriptor) & 0xFF,
                        (sizeof(hid_report_descriptor) >> 8) & 0xFF
                };
                int len = (setup->wLength < sizeof(hid_desc)) ? setup->wLength : sizeof(hid_desc);
                memcpy(response, hid_desc, len);
                response_len = len;
            }
            else if (desc_type == 0x22) {  // HID Report Descriptor
                LOGI("Sending HID Report Descriptor");
                int len = (setup->wLength < sizeof(hid_report_descriptor)) ?
                          setup->wLength : sizeof(hid_report_descriptor);
                memcpy(response, hid_report_descriptor, len);
                response_len = len;
            }
        }
        else if (setup->bRequest == USB_REQ_GET_STATUS) {
            LOGI("GET_STATUS");
            response[0] = 0x00;
            response[1] = 0x00;
            response_len = 2;
        }
        else if (setup->bRequest == USB_REQ_SET_CONFIGURATION) {
            LOGI("SET_CONFIGURATION: value=%d", setup->wValue & 0xFF);
            response_len = 0;  // ACK
        }
        else if (setup->bRequest == USB_REQ_GET_CONFIGURATION) {
            LOGI("GET_CONFIGURATION");
            response[0] = 0x01;
            response_len = 1;
        }
        else if (setup->bRequest == USB_REQ_SET_INTERFACE) {
            LOGI("SET_INTERFACE: interface=%d alt=%d",
                 setup->wIndex, setup->wValue);
            response_len = 0;  // ACK
        }
        else if (setup->bRequest == USB_REQ_GET_INTERFACE) {
            LOGI("GET_INTERFACE");
            response[0] = 0x00;
            response_len = 1;
        }
    }
    else if (request_type == USB_TYPE_CLASS) {
        LOGI("CLASS request: 0x%02x", setup->bRequest);

        if (setup->bRequest == 0x01) {  // GET_REPORT
            LOGI("GET_REPORT");
            memset(response, 0, 32);
            response[0] = 0x53;
            response_len = (setup->wLength < 32) ? setup->wLength : 32;
        }
        else if (setup->bRequest == 0x09) {  // SET_REPORT
            LOGI("SET_REPORT");
            response_len = 0;
        }
        else if (setup->bRequest == 0x0A) {  // SET_IDLE
            LOGI("SET_IDLE");
            response_len = 0;
        }
        else if (setup->bRequest == 0x0B) {  // SET_PROTOCOL
            LOGI("SET_PROTOCOL");
            response_len = 0;
        }
        else if (setup->bRequest == 0x03) {  // GET_PROTOCOL
            LOGI("GET_PROTOCOL");
            response[0] = 0x01;
            response_len = 1;
        }
        else {
            LOGI("Unknown HID class request: 0x%02x", setup->bRequest);
        }
    }

    // Send response
    if (response_len >= 0) {
        if (response_len == 0) {
            int ret = write(g_portal.ep0_fd, NULL, 0);  // or write(g_portal.ep0_fd, "", 0);
            LOGI("Sent ZLP ACK (ret=%d)", ret);
        } else {
            int ret = write(g_portal.ep0_fd, response, response_len);
            if (ret < 0) {
                LOGE("Failed to write response: %d (%s)", errno, strerror(errno));
            } else {
                LOGI("Sent %d bytes", ret);
            }
        }
    } else {
        LOGI("STALL");
    }
}

static void handle_portal_command(const uint8_t *data, size_t len) {
    if (len < 1) return;

    uint8_t cmd = data[0];
    uint8_t response[32];
    memset(response, 0, sizeof(response));
    int response_len = 0;

    LOGI("Portal command: 0x%02x, len=%zu", cmd, len);

    switch (cmd) {
        case 0x41: // Activate
            LOGI("Activate portal");
            response[0] = 0x41;
            response[1] = 0x01;
            response[2] = 0xFF;
            response[3] = 0x77;
            response_len = 32;  // Pad to 64 bytes
            break;

        case 0x43: // Set LED color
            if (len >= 4) {
                uint8_t r = data[1];
                uint8_t g = data[2];
                uint8_t b = data[3];
                LOGI("Set LED: R=%d G=%d B=%d", r, g, b);
                response[0] = 0x43;
                response[1] = r;
                response[2] = g;
                response[3] = b;
                response_len = 32;
            }
            break;

        case 0x4A: // Query (sound related?)
            LOGI("Query command");
            response[0] = 0x4A;
            response_len = 32;
            break;

        case 0x4C: // Traptanium portal LED control
            if (len >= 5) {
                uint8_t side = data[1];  // 0x00=right, 0x02=left
                LOGI("Traptanium LED control: side=%d", side);
                // No response
            }
            break;

        case 0x4D: // Speaker control
            if (len >= 2 && data[1] > 0) {
                LOGI("Activate speaker");
                response[0] = 0x4D;
                response[1] = 0x01;  // Has speaker
                response_len = 32;
            } else {
                LOGI("Deactivate speaker");
                response[0] = 0x4D;
                response_len = 32;
            }
            break;

        case 0x51: // Read Skylander
            if (len >= 3) {
                uint8_t slot_query = data[1];
                uint8_t block = data[2];
                // Convert query format (0x20/0x21) to slot index (0/1)
                uint8_t slot = (slot_query & 0x0F) - 0;
                if (slot_query == 0x20) slot = 0;
                else if (slot_query == 0x21) slot = 1;

                LOGI("Read Skylander: query=0x%02x block=%d slot=%d", slot_query, block, slot);

                if (slot < MAX_SLOTS && g_portal.slots[slot].present) {
                    response[0] = 0x51;
                    response[1] = 0x10 + slot;  // Response format: 0x10/0x11
                    response[2] = block;

                    size_t offset = block * 16;
                    if (offset + 16 <= g_portal.slots[slot].size) {
                        memcpy(&response[3], &g_portal.slots[slot].data[offset], 16);
                    }
                    response_len = 32;
                }
            }
            break;

        case 0x52: { // Shutdown/restart
            LOGI("Shutdown/restart");
            response[0] = 0x52;
            response[1] = 0x02;
            response[2] = 0x0A;
            response[3] = 0x05;
            response[4] = 0x08;
            response_len = 32;
            break;

        case 0x53: // Sense (manual query)
            LOGI("Manual sense query");
            response[0] = 0x53;
            // Bitmask (little endian, 4 bytes)
            uint32_t mask = 0;
            for (int i = 0; i < MAX_SLOTS; i++) {
                if (g_portal.slots[i].present) {
                    mask |= (1 << i);
                }
            }
            response[1] = mask & 0xFF;
            response[2] = (mask >> 8) & 0xFF;
            response[3] = (mask >> 16) & 0xFF;
            response[4] = (mask >> 24) & 0xFF;
            response[5] = 0x00;  // Counter
            response[6] = 0x01;
            response_len = 32;
            break;
        }
        case 0x56: // Unknown V command
            LOGI("V command");
            response[0] = 0x56;
            response_len = 32;
            break;

        case 0x57: // Write Skylander
            if (len >= 19) {
                uint8_t slot_query = data[1];
                uint8_t block = data[2];
                uint8_t slot = (slot_query == 0x20) ? 0 : 1;

                LOGI("Write Skylander: query=0x%02x block=%d slot=%d", slot_query, block, slot);

                if (slot < MAX_SLOTS && g_portal.slots[slot].present) {
                    size_t offset = block * 16;
                    if (offset + 16 <= g_portal.slots[slot].size) {
                        memcpy(&g_portal.slots[slot].data[offset], &data[3], 16);
                    }
                    response[0] = 0x57;
                    response[1] = 0x10 + slot;
                    response[2] = block;
                    memcpy(&response[3], &data[3], 16);
                    response_len = 32;
                }
            }
            break;

        default:
            LOGI("Unknown command: 0x%02x", cmd);
            break;
    }

    if (response_len > 0) {
        int ret = write(g_portal.ep_in_fd, response, response_len);
        if (ret < 0) {
            LOGE("Failed to write response: %d (%s)", errno, strerror(errno));
        } else {
            LOGI("Sent response: %d bytes (cmd 0x%02x)", ret, response[0]);
        }
    }
}

// Signal handler for clean shutdown
void signal_handler(int signum) {
    printf("Received signal %d, shutting down...\n", signum);
    g_portal.running = false;
}

int main(int argc, char *argv[]) {
    // Redirect stderr to a log file for debugging
    FILE* log_file = fopen("/data/local/tmp/portal_daemon.log", "w");
    if (log_file) {
        dup2(fileno(log_file), STDERR_FILENO);
        setbuf(stderr, NULL); // Unbuffered
    }

    fprintf(stderr, "=== Portal Daemon Starting ===\n");
    fprintf(stderr, "PID: %d, UID: %d, EUID: %d\n", getpid(), getuid(), geteuid());

    if (geteuid() != 0) {
        fprintf(stderr, "ERROR: Must run as root! Current EUID=%d\n", geteuid());
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);  // ADD THIS - ignore broken pipe

    memset(&g_portal, 0, sizeof(g_portal));
    g_portal.running = true;
    g_portal.ep0_fd = -1;
    g_portal.ep_in_fd = -1;
    g_portal.ep_out_fd = -1;

    // Open ep0
    printf("Opening ep0...\n");
    fflush(stdout);
    for (int retry = 0; retry < 15; retry++) {
        g_portal.ep0_fd = open("/dev/usb-ffs/portal0/ep0", O_RDWR);
        if (g_portal.ep0_fd >= 0) {
            printf("ep0 opened successfully: fd=%d\n", g_portal.ep0_fd);
            fflush(stdout);
            break;
        }
        fprintf(stderr, "Failed to open ep0 (attempt %d/15): %d (%s)\n",
                retry + 1, errno, strerror(errno));
        sleep(1);
    }

    if (g_portal.ep0_fd < 0) {
        fprintf(stderr, "FATAL: Failed to open ep0\n");
        return 1;
    }

    // Write descriptors
    printf("Writing descriptors...\n");
    fflush(stdout);
    if (write_descriptors(g_portal.ep0_fd) < 0) {
        fprintf(stderr, "FATAL: Failed to write descriptors\n");
        close(g_portal.ep0_fd);
        return 1;
    }

    printf("READY\n");
    fflush(stdout);

    // Wait for data endpoints
    printf("Waiting for data endpoints...\n");
    fflush(stdout);
    for (int i = 0; i < 30; i++) {
        if (access("/dev/usb-ffs/portal0/ep1", F_OK) == 0 &&
            access("/dev/usb-ffs/portal0/ep2", F_OK) == 0) {
            printf("Data endpoints detected!\n");
            fflush(stdout);
            break;
        }
        if (i == 29) {
            fprintf(stderr, "FATAL: Data endpoints never appeared\n");
            close(g_portal.ep0_fd);
            return 1;
        }
        sleep(1);
    }

    // Open data endpoints
    printf("Opening data endpoints...\n");
    fflush(stdout);
    for (int retry = 0; retry < 15; retry++) {
        g_portal.ep_in_fd = open("/dev/usb-ffs/portal0/ep1", O_RDWR | O_NONBLOCK);
        if (g_portal.ep_in_fd >= 0) {
            printf("ep1 opened: fd=%d\n", g_portal.ep_in_fd);
            fflush(stdout);
            break;
        }
        usleep(2000000);
    }

    for (int retry = 0; retry < 15; retry++) {
        g_portal.ep_out_fd = open("/dev/usb-ffs/portal0/ep2", O_RDWR | O_NONBLOCK);
        if (g_portal.ep_out_fd >= 0) {
            printf("ep2 opened: fd=%d\n", g_portal.ep_out_fd);
            fflush(stdout);
            break;
        }
        usleep(2000000);
    }

    if (g_portal.ep_in_fd < 0 || g_portal.ep_out_fd < 0) {
        fprintf(stderr, "FATAL: Failed to open data endpoints\n");
        if (g_portal.ep_in_fd >= 0) close(g_portal.ep_in_fd);
        close(g_portal.ep0_fd);
        return 1;
    }

    printf("ALL_READY\n");
    fflush(stdout);

    printf("Entering main loop...\n");
    fflush(stdout);

    uint8_t buffer[256];
    fd_set rfds;
    struct timeval tv;
    int idle_count = 0;
    time_t last_report_time = time(NULL);  // ADD THIS

    while (g_portal.running) {
        FD_ZERO(&rfds);
        FD_SET(g_portal.ep0_fd, &rfds);
        FD_SET(g_portal.ep_out_fd, &rfds);

        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int maxfd = (g_portal.ep0_fd > g_portal.ep_out_fd) ?
                    g_portal.ep0_fd : g_portal.ep_out_fd;
        int ret = select(maxfd + 1, &rfds, NULL, NULL, &tv);

        if (ret < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "select error: %d (%s)\n", errno, strerror(errno));
            break;
        }

        // CRITICAL: Send periodic sense reports to keep Windows happy
        time_t now = time(NULL);
        if (now - last_report_time >= 5) {  // Every 5 seconds
            printf("Sending periodic sense report...\n");
            fflush(stdout);

            uint8_t sense[32];
            memset(sense, 0, sizeof(sense));
            sense[0] = 0x53;

            uint32_t mask = 0;
            for (int i = 0; i < MAX_SLOTS; i++) {
                if (g_portal.slots[i].present) mask |= (1 << i);
            }
            sense[1] = mask & 0xFF;
            sense[2] = (mask >> 8) & 0xFF;
            sense[3] = (mask >> 16) & 0xFF;
            sense[4] = (mask >> 24) & 0xFF;
            sense[5] = 0x00;
            sense[6] = 0x01;

            int write_ret = write(g_portal.ep_in_fd, sense, 32);
            if (write_ret < 0) {
                fprintf(stderr, "Failed to send periodic report: %d (%s)\n",
                        errno, strerror(errno));
            } else {
                printf("Periodic sense sent: %d bytes\n", write_ret);
            }

            last_report_time = now;
        }

        if (ret == 0) {
            idle_count++;
            if (idle_count % 10 == 0) {
                printf("Still alive (idle for %d seconds)...\n", idle_count);
                fflush(stdout);
            }
            continue;
        }

        idle_count = 0;

        // Handle ep0 events
        if (FD_ISSET(g_portal.ep0_fd, &rfds)) {
            struct usb_functionfs_event event;
            int n = read(g_portal.ep0_fd, &event, sizeof(event));

            if (n == sizeof(event)) {
                printf("ep0 event: type=%d\n", event.type);
                fflush(stdout);

                switch (event.type) {
                    case FUNCTIONFS_SETUP:
                        printf("SETUP request\n");
                        fflush(stdout);
                        handle_setup_request(&event.u.setup);
                        break;
                    case FUNCTIONFS_ENABLE: {
                        printf("Device ENABLED by host - sending initial sense\n");
                        fflush(stdout);

                        // Send initial sense
                        uint8_t sense[32];
                        memset(sense, 0, sizeof(sense));
                        sense[0] = 0x53;

                        uint32_t mask = 0;
                        for (int i = 0; i < MAX_SLOTS; i++) {
                            if (g_portal.slots[i].present) mask |= (1 << i);
                        }
                        sense[1] = mask & 0xFF;
                        sense[2] = (mask >> 8) & 0xFF;
                        sense[3] = (mask >> 16) & 0xFF;
                        sense[4] = (mask >> 24) & 0xFF;
                        sense[5] = 0x00;
                        sense[6] = 0x01;

                        write(g_portal.ep_in_fd, sense, 32);
                        break;
                    }
                    case FUNCTIONFS_DISABLE:
                        printf("Device DISABLED by host\n");
                        fflush(stdout);
                        break;
                    case FUNCTIONFS_UNBIND:
                        printf("Device UNBOUND - exiting\n");
                        fflush(stdout);
                        g_portal.running = false;
                        break;
                    default:
                        printf("Unknown event: %d\n", event.type);
                        fflush(stdout);
                        break;
                }
            } else if (n < 0) {
                if (errno != EAGAIN && errno != EINTR) {
                    fprintf(stderr, "ep0 read error: %d (%s)\n", errno, strerror(errno));
                    break;
                }
            }
        }

        // Handle OUT endpoint
        if (FD_ISSET(g_portal.ep_out_fd, &rfds)) {
            int n = read(g_portal.ep_out_fd, buffer, sizeof(buffer));
            if (n > 0) {
                printf("Received %d bytes from host\n", n);
                fflush(stdout);
                handle_portal_command(buffer, n);
            } else if (n < 0) {
                if (errno == ESHUTDOWN || errno == ECONNRESET || errno == ENOTCONN) {
                    fprintf(stderr, "Transport shutdown - host disconnected\n");
                    // Don't exit - wait for reconnect
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    fprintf(stderr, "ep_out read error: %d (%s)\n", errno, strerror(errno));
                }
            }
        }

        struct stat st;
        if (fstat(g_portal.ep0_fd, &st) < 0) {
            fprintf(stderr, "FATAL: ep0_fd invalid: %s\n", strerror(errno));
            break;
        }
        if (fstat(g_portal.ep_in_fd, &st) < 0) {
            fprintf(stderr, "FATAL: ep_in_fd invalid: %s\n", strerror(errno));
            break;
        }
        if (fstat(g_portal.ep_out_fd, &st) < 0) {
            fprintf(stderr, "FATAL: ep_out_fd invalid: %s\n", strerror(errno));
            break;
        }
    }

    // Cleanup
    printf("Shutting down...\n");
    fflush(stdout);
    if (g_portal.ep_in_fd >= 0) close(g_portal.ep_in_fd);
    if (g_portal.ep_out_fd >= 0) close(g_portal.ep_out_fd);
    if (g_portal.ep0_fd >= 0) close(g_portal.ep0_fd);

    fprintf(stderr, "=== Daemon Exiting: running=%d ===\n", g_portal.running);
    fclose(log_file);
    return 0;
}