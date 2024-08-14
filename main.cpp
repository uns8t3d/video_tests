#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <SDL2/SDL.h>
#include <sys/mman.h>
#include <jpeglib.h>
#include <cstring>

const char *DEVICE = "/dev/video4";
const int WIDTH = 1920;
const int HEIGHT = 1080;

bool decodeMJPEGtoRGB(unsigned char *mjpegData, int jpegSize, unsigned char *rgbData) {
    jpeg_decompress_struct cinfo;
    jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    jpeg_mem_src(&cinfo, mjpegData, jpegSize);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        std::cerr << "Error reading JPEG header" << std::endl;
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    jpeg_start_decompress(&cinfo);

    int row_stride = cinfo.output_width * cinfo.output_components;
    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char *buffer_array[1];
        buffer_array[0] = rgbData + (cinfo.output_scanline) * row_stride;
        jpeg_read_scanlines(&cinfo, buffer_array, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    return true;
}

void processFrame(unsigned char *rgbData, int width, int height) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned char *pixel = rgbData + (y * width + x) * 3;
            pixel[0] = 255 - pixel[0]; // Invert Red
            pixel[1] = 255 - pixel[1]; // Invert Green
            pixel[2] = 255 - pixel[2]; // Invert Blue
        }
    }
}

int main() {
    int fd = open(DEVICE, O_RDWR);
    if (fd == -1) {
        std::cerr << "Error opening camera device" << std::endl;
        return -1;
    }

    v4l2_format format = {};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = WIDTH;
    format.fmt.pix.height = HEIGHT;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    format.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &format) == -1) {
        std::cerr << "Error setting video format" << std::endl;
        close(fd);
        return -1;
    }

    v4l2_requestbuffers req = {};
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        std::cerr << "Error requesting buffer" << std::endl;
        close(fd);
        return -1;
    }

    v4l2_buffer buffer = {};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = 0;

    if (ioctl(fd, VIDIOC_QUERYBUF, &buffer) == -1) {
        std::cerr << "Error requesting buffer" << std::endl;
        close(fd);
        return -1;
    }

    void* buffer_start = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buffer.m.offset);
    if (buffer_start == MAP_FAILED) {
        std::cerr << "Buffer mapping error" << std::endl;
        close(fd);
        return -1;
    }

    // Place buffer to queue
    if (ioctl(fd, VIDIOC_QBUF, &buffer) == -1) {
        std::cerr << "Error setting buffer in queue" << std::endl;
        munmap(buffer_start, buffer.length);
        close(fd);
        return -1;
    }

    // Launch capturing
    if (ioctl(fd, VIDIOC_STREAMON, &buffer.type) == -1) {
        std::cerr << "Error launching capturing" << std::endl;
        munmap(buffer_start, buffer.length);
        close(fd);
        return -1;
    }

    // SDL Init
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL init error: " << SDL_GetError() << std::endl;
        munmap(buffer_start, buffer.length);
        close(fd);
        return -1;
    }

    SDL_Window *window = SDL_CreateWindow("Video Capture", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WIDTH, HEIGHT, 0);
    if (!window) {
        std::cerr << "SDL window creation error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        munmap(buffer_start, buffer.length);
        close(fd);
        return -1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);

    unsigned char *rgbData = new unsigned char[WIDTH * HEIGHT * 3];

    // Capturing and processing frame
    while (true) {
        if (ioctl(fd, VIDIOC_DQBUF, &buffer) == -1) {
            std::cerr << "Error capturing frame" << std::endl;
            break;
        }

        // Decode MJPEG to RGB
        if (!decodeMJPEGtoRGB(static_cast<unsigned char*>(buffer_start), buffer.bytesused, rgbData)) {
            std::cerr << "Error decoding MJPEG" << std::endl;
            break;
        }

        // frame processing
        // processFrame(rgbData, WIDTH, HEIGHT);

        // Define texture and rendering
        SDL_UpdateTexture(texture, nullptr, rgbData, WIDTH * 3);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        if (ioctl(fd, VIDIOC_QBUF, &buffer) == -1) {
            std::cerr << "Error buffering frame" << std::endl;
            break;
        }

        SDL_Event e;
        if (SDL_PollEvent(&e) && e.type == SDL_QUIT) {
            break;
        }
    }

    // Capturing end
    ioctl(fd, VIDIOC_STREAMOFF, &buffer.type);

    // Free resources
    delete[] rgbData;
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    munmap(buffer_start, buffer.length);
    close(fd);

    return 0;
}
