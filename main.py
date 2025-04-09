import v4l2capture
import select
import threading
import time
import os
import sys

# Try to import resource for Unix-like systems
try:
    import resource
    has_resource = True
except ImportError:
    has_resource = False

running = True

def get_memory_usage():
    """Get the current memory usage of this process in MB using native methods"""
    if has_resource:
        # Unix-like systems (Linux, macOS)
        rusage = resource.getrusage(resource.RUSAGE_SELF)
        # maxrss returns KB on Linux, bytes on macOS, so we normalize to MB
        if sys.platform == 'darwin':
            return rusage.ru_maxrss / 1024.0 / 1024.0  # macOS returns bytes
        else:
            return rusage.ru_maxrss / 1024.0  # Linux returns KB
    
    # Windows or other platforms - try /proc if available
    try:
        with open(f'/proc/{os.getpid()}/status') as f:
            for line in f:
                if line.startswith('VmRSS:'):
                    # Extract the value in KB and convert to MB
                    return float(line.split()[1]) / 1024.0
    except (FileNotFoundError, IOError):
        pass
        
    # If all methods failed
    return 0.0  # Return 0 as we couldn't get memory usage

def capture_camera(device, name):
    video = v4l2capture.Video_device(device)
    size_x, size_y = 320, 240
    video.set_format(size_x, size_y)
    video.create_buffers(1)
    video.queue_all_buffers()
    video.start()

    print(f"🎥 {name} started at {device} ({size_x}x{size_y})")

    frame_count = 0

    try:
        while running:
            select.select((video,), (), ())
            image_data = video.read_and_queue()
            frame_count += 1

            # Minimal processing (can extend to save, stream, etc.)
            mem_usage = get_memory_usage()
            print(f"📸 {name}: Frame {frame_count} | Memory: {mem_usage:.2f} MB")
            time.sleep(0.1)
    except Exception as e:
        print(f"❌ Error in {name}: {e}")
    finally:
        video.close()
        print(f"🛑 {name} stopped. Total frames: {frame_count}")

# Launch both cameras in threads
t0 = threading.Thread(target=capture_camera, args=("/dev/video0", "Camera 0"))
# t2 = threading.Thread(target=capture_camera, args=("/dev/video2", "Camera 2"))

t0.start()
# t2.start()

try:
    while True:
        mem_usage = get_memory_usage()
        print(f"💾 Total memory usage: {mem_usage:.2f} MB")
        time.sleep(1)
except KeyboardInterrupt:
    print("🔻 KeyboardInterrupt received. Stopping...")
    running = False
    t0.join()
    # t2.join()
    print("✅ All streams stopped.")
