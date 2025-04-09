#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <sys/select.h>
#include <sys/time.h>
#include <pthread.h>
#include <signal.h>
#include <sys/resource.h>

volatile int running = 1;

float get_memory_usage()
{
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss / 1024.0f;
}

void *capture_camera(void *arg)
{
    const char *device = (const char *)arg;
    int fd = open(device, O_RDWR);
    if (fd < 0)
    {
        perror("Failed to open video device");
        return NULL;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 320;
    fmt.fmt.pix.height = 240;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        perror("Setting Pixel Format");
        close(fd);
        return NULL;
    }

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0)
    {
        perror("Requesting Buffer");
        close(fd);
        return NULL;
    }

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0)
    {
        perror("Querying Buffer");
        close(fd);
        return NULL;
    }

    void *buffer = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
    if (buffer == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        return NULL;
    }

    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0)
    {
        perror("Queue Buffer");
        close(fd);
        return NULL;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0)
    {
        perror("Start Capture");
        close(fd);
        return NULL;
    }

    int frame_count = 0;
    while (running)
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        struct timeval tv = {1, 0};
        int r = select(fd + 1, &fds, NULL, NULL, &tv);
        if (r == -1)
        {
            perror("Waiting for Frame");
            break;
        }

        if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0)
        {
            perror("Retrieving Frame");
            break;
        }

        frame_count++;
        printf("[%s] Frame %d | Memory: %.2f MB\n", device, frame_count, get_memory_usage());

        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0)
        {
            perror("Requeueing Buffer");
            break;
        }
        usleep(100000); // sleep 100ms
    }

    ioctl(fd, VIDIOC_STREAMOFF, &type);
    munmap(buffer, buf.length);
    close(fd);
    printf("[%s] Capture stopped. Total frames: %d\n", device, frame_count);
    return NULL;
}

void handle_signal(int sig)
{
    running = 0;
}

int main()
{
    signal(SIGINT, handle_signal);

    pthread_t thread0;
    pthread_create(&thread0, NULL, capture_camera, "/dev/video0");

    while (running)
    {
        float mem_usage = get_memory_usage();
        printf("[System] Memory Usage: %.2f MB\n", mem_usage);
        sleep(1);
    }

    pthread_join(thread0, NULL);
    printf("[System] Capture completed.\n");
    return 0;
}
