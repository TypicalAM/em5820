#ifndef EM5820_HPP
#define EM5820_HPP

#include <libusb-1.0/libusb.h>
#include <stdexcept>
#include <vector>

namespace em5820 {

class Printer {
public:
  enum class Alignment { LEFT, CENTER, RIGHT };
  enum class BitmapMode { NORMAL, WIDE, TALL, HUGE };

  Printer() = default;

  ~Printer() {
    if (dev_handle) {
      libusb_release_interface(dev_handle, 0);
      libusb_close(dev_handle);
      dev_handle = nullptr;
    }

    if (ctx) {
      libusb_exit(ctx);
      ctx = nullptr;
    }
  }

  void open_usb() {
    if (libusb_init(&ctx) < 0) {
      ctx = nullptr;
      throw std::runtime_error("Failed to initialize libusb");
    }

    libusb_device **dev_list = nullptr;
    ssize_t count = libusb_get_device_list(ctx, &dev_list);
    if (count < 0) {
      libusb_exit(ctx);
      ctx = nullptr;
      throw std::runtime_error("Failed to get USB device list");
    }

    libusb_device_descriptor desc;
    libusb_device *target_device = nullptr;
    for (ssize_t i = 0; i < count; ++i) {
      libusb_device *device = dev_list[i];
      libusb_get_device_descriptor(device, &desc);
      if (desc.idVendor == USB_VENDOR && desc.idProduct == USB_PRODUCT) {
        target_device = device;
        break;
      }
    }

    if (!target_device) {
      libusb_free_device_list(dev_list, 1);
      libusb_exit(ctx);
      ctx = nullptr;
      throw std::runtime_error("Target USB device not found");
    }

    if (libusb_open(target_device, &dev_handle) < 0) {
      libusb_free_device_list(dev_list, 1);
      libusb_exit(ctx);
      ctx = nullptr;
      throw std::runtime_error("Failed to open USB device");
    }

    libusb_free_device_list(dev_list, 1);

    libusb_set_auto_detach_kernel_driver(dev_handle, 1);

    int ret = libusb_claim_interface(dev_handle, 0);
    if (ret < 0) {
      cleanup();
      throw std::runtime_error("Failed to claim interface: " +
                               std::string(libusb_error_name(ret)));
    }
  }

  uint16_t write_bytes(const std::vector<uint8_t> &data) {
    int ret, transferred;
    unsigned char buffer[64];
    do {
      ret = libusb_bulk_transfer(dev_handle, BULK_ENDPOINT_IN, buffer,
                                 sizeof(buffer), &transferred, 100);
    } while (ret == 0 && transferred > 0);

    ret = libusb_bulk_transfer(dev_handle, BULK_ENDPOINT_OUT,
                               const_cast<unsigned char *>(data.data()),
                               data.size(), &transferred, TIMEOUT);
    if (ret != 0 || transferred != data.size())
      throw std::runtime_error("Failed transfer data: " +
                               std::string(libusb_error_name(ret)));

    return transferred;
  }

  uint16_t reset() { return write_bytes({0x1b, 0x40}); }

  uint16_t set_text_scale(uint8_t horizontal, uint8_t vertical) {
    uint8_t scale = vertical & 0xf | ((horizontal & 0xf) << 4);
    return write_bytes({0x1d, 0x21, scale});
  }

  uint16_t set_print_text_type(uint8_t print_type) {
    return write_bytes({0x1b, 0x21, print_type});
  }

  uint16_t write_string(const std::string &str) {
    return write_bytes(std::vector<uint8_t>(str.begin(), str.end()));
  }

  uint16_t feed_dots(uint8_t dots) { return write_bytes({0x1b, 0x4a, dots}); }

  uint16_t feed_lines(uint8_t lines) {
    return write_bytes({0x1b, 0x64, lines});
  }

  uint16_t set_horizontal_absolute_print_position(uint16_t pos) {
    return write_bytes({0x1b, 0x24, static_cast<uint8_t>(pos),
                        static_cast<uint8_t>(pos >> 8)});
  }

  uint16_t set_alignment(Alignment allign) {
    return write_bytes({0x1b, 0x61, static_cast<uint8_t>(allign)});
  }

  uint16_t set_underline(uint8_t thickness) {
    if (thickness > 2)
      thickness = 2;
    return write_bytes({0x1b, 0x2d, thickness});
  }

  uint16_t print_bitmap(BitmapMode mode, uint16_t width, uint16_t height,
                        const std::vector<uint8_t> &bitmap) {

    uint8_t width_first = (width / 8) & 0x00ff;
    uint8_t width_second = ((width / 8) >> 8) & 0x00ff;
    uint8_t height_first = height & 0x00ff;
    uint8_t height_second = (height >> 8) & 0x00ff;
    std::vector<uint8_t> data{
        0x1d,        0x76,         0x30,         static_cast<uint8_t>(mode),
        width_first, width_second, height_first, height_second};
    data.insert(data.end(), bitmap.begin(), bitmap.end());
    return write_bytes(data);
  }

  static inline constexpr uint8_t enable_ascii_9x17(uint8_t optbit) {
    return optbit | 0x01;
  }

  static inline constexpr uint8_t enable_ascii_12x24(uint8_t optbit) {
    return optbit & 0xFE;
  }

  static inline constexpr uint8_t enable_bold(uint8_t optbit) {
    return optbit | 0x08;
  }

  static inline constexpr uint8_t enable_double_height(uint8_t optbit) {
    return optbit | 0x10;
  }

  static inline constexpr uint8_t enable_double_wide(uint8_t optbit) {
    return optbit | 0x20;
  }

  static inline constexpr uint8_t enable_underline(uint8_t optbit) {
    return optbit | 0x80;
  }

  void cleanup() {
    if (dev_handle) {
      libusb_release_interface(dev_handle, 0);
      libusb_close(dev_handle);
      dev_handle = nullptr;
    }

    if (ctx) {
      libusb_exit(ctx);
      ctx = nullptr;
    }
  }

private:
  static constexpr uint64_t USB_VENDOR = 10473;
  static constexpr uint64_t USB_PRODUCT = 649;

  static constexpr uint64_t BULK_ENDPOINT_IN = 0x81;
  static constexpr uint64_t BULK_ENDPOINT_OUT = 0x03;
  static constexpr uint64_t TIMEOUT = 5000;

  libusb_context *ctx = nullptr;
  libusb_device_handle *dev_handle = nullptr;
};
} // namespace em5820

#endif // EM5820_HPP
