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
        std::cerr << "Ошибка чтения заголовка JPEG" << std::endl;
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

// Ваша функция обработки кадра
void processFrame(unsigned char *rgbData, int width, int height) {
    // Пример обработки: инвертирование цветов
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
    // Открываем устройство камеры
    int fd = open(DEVICE, O_RDWR);
    if (fd == -1) {
        std::cerr << "Ошибка открытия устройства камеры" << std::endl;
        return -1;
    }

    // Настраиваем формат видео
    v4l2_format format = {};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = WIDTH;
    format.fmt.pix.height = HEIGHT;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    format.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &format) == -1) {
        std::cerr << "Ошибка установки формата видео" << std::endl;
        close(fd);
        return -1;
    }

    // Запрос буферов
    v4l2_requestbuffers req = {};
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        std::cerr << "Ошибка запроса буферов" << std::endl;
        close(fd);
        return -1;
    }

    // Маппинг буфера
    v4l2_buffer buffer = {};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = 0;

    if (ioctl(fd, VIDIOC_QUERYBUF, &buffer) == -1) {
        std::cerr << "Ошибка запроса буфера" << std::endl;
        close(fd);
        return -1;
    }

    void* buffer_start = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buffer.m.offset);
    if (buffer_start == MAP_FAILED) {
        std::cerr << "Ошибка маппинга буфера" << std::endl;
        close(fd);
        return -1;
    }

    // Помещаем буфер в очередь
    if (ioctl(fd, VIDIOC_QBUF, &buffer) == -1) {
        std::cerr << "Ошибка помещения буфера в очередь" << std::endl;
        munmap(buffer_start, buffer.length);
        close(fd);
        return -1;
    }

    // Запуск захвата
    if (ioctl(fd, VIDIOC_STREAMON, &buffer.type) == -1) {
        std::cerr << "Ошибка запуска захвата" << std::endl;
        munmap(buffer_start, buffer.length);
        close(fd);
        return -1;
    }

    // Инициализация SDL
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "Ошибка инициализации SDL: " << SDL_GetError() << std::endl;
        munmap(buffer_start, buffer.length);
        close(fd);
        return -1;
    }

    SDL_Window *window = SDL_CreateWindow("Video Capture", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WIDTH, HEIGHT, 0);
    if (!window) {
        std::cerr << "Ошибка создания окна SDL: " << SDL_GetError() << std::endl;
        SDL_Quit();
        munmap(buffer_start, buffer.length);
        close(fd);
        return -1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);

    unsigned char *rgbData = new unsigned char[WIDTH * HEIGHT * 3];

    // Захват и обработка видео
    while (true) {
        if (ioctl(fd, VIDIOC_DQBUF, &buffer) == -1) {
            std::cerr << "Ошибка захвата кадра" << std::endl;
            break;
        }

        // Декодирование MJPEG в RGB
        if (!decodeMJPEGtoRGB(static_cast<unsigned char*>(buffer_start), buffer.bytesused, rgbData)) {
            std::cerr << "Ошибка декодирования MJPEG" << std::endl;
            break;
        }

        // Обработка кадра
        // processFrame(rgbData, WIDTH, HEIGHT);

        // Обновление текстуры и рендеринг
        SDL_UpdateTexture(texture, nullptr, rgbData, WIDTH * 3);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        if (ioctl(fd, VIDIOC_QBUF, &buffer) == -1) {
            std::cerr << "Ошибка буферизации кадра" << std::endl;
            break;
        }

        SDL_Event e;
        if (SDL_PollEvent(&e) && e.type == SDL_QUIT) {
            break;
        }
    }

    // Завершение захвата
    ioctl(fd, VIDIOC_STREAMOFF, &buffer.type);

    // Освобождение ресурсов
    delete[] rgbData;
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    munmap(buffer_start, buffer.length);
    close(fd);

    return 0;
}
