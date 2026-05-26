#include <stdint.h>      // uint32_t için şart
#include "kernel_utils.h" // play_sound ve nosound tanımları burada olmalı

/* 1. usleep tanımı */
void usleep(uint32_t useconds) {
    // 150 yerine 2000 veya 2500 civarı bir değer T6670 için daha dengeli olacaktır.
    for (volatile uint32_t i = 0; i < useconds * 2000; i++) {
        __asm__ volatile ("nop");
    }
}

/* 2. ioctl tanımı */
void ioctl(int fd, int cmd, int tick) {
    (void)fd; (void)cmd;
    if (tick == 0) {
        nosound();
    } else {
        play_sound(1193180 / tick);
    }
}

/* 3. play fonksiyonunu listenin üstünde tanımlamalıyız (implicit error çözümü) */
void play(int fd, double freq, int ms) {
    if (freq > 20.0) {
        int tick = (int)(1193180 / freq);
        ioctl(fd, 0, tick);
    } else {
        ioctl(fd, 0, 0);
    }
    usleep(ms * 1000);
    ioctl(fd, 0, 0);
}

// main() ismini play_monkey_island() yapıyoruz ki kernel çağırabilsin
void play_monkey_island() {
    int fd = 0;
    play(fd, 15804.26, 200); play(fd, 19.44, 50); play(fd, 987.76, 50);
    play(fd, 659.25, 25); play(fd, 493.88, 25); play(fd, 311.12, 25);
    play(fd, 155.56, 25); play(fd, 19.44, 50); play(fd, 349.22, 25);
    play(fd, 19.44, 100); play(fd, 155.56, 25); play(fd, 277.18, 25);
    play(fd, 391.99, 25); play(fd, 38.89, 25); play(fd, 19.44, 75);
    play(fd, 261.62, 25); play(fd, 369.99, 25); play(fd, 155.56, 100);
    play(fd, 19.44, 25); play(fd, 493.88, 50); play(fd, 19.44, 125);
    play(fd, 391.99, 25); play(fd, 38.89, 25); play(fd, 19.44, 100);
    play(fd, 233.08, 25); play(fd, 369.99, 25); play(fd, 38.89, 25);
    play(fd, 19.44, 75); play(fd, 220.0, 25); play(fd, 261.62, 25);
    play(fd, 369.99, 25); play(fd, 19.44, 100); play(fd, 38.89, 25);
    play(fd, 138.59, 25); play(fd, 19.44, 125); play(fd, 277.18, 25);
    play(fd, 293.66, 25); play(fd, 19.44, 100); play(fd, 184.99, 25);
    play(fd, 195.99, 25); play(fd, 369.99, 25); play(fd, 138.59, 25);
    play(fd, 155.56, 25); play(fd, 19.44, 50); play(fd, 195.99, 25);
    play(fd, 293.66, 25); play(fd, 138.59, 25); play(fd, 466.16, 25);
    play(fd, 659.25, 25); play(fd, 138.59, 50); play(fd, 987.76, 25);
    play(fd, 155.56, 150); play(fd, 311.12, 25); play(fd, 19.44, 100);
    play(fd, 195.99, 25); play(fd, 261.62, 25); play(fd, 369.99, 25);
    play(fd, 155.56, 50); play(fd, 19.44, 50); play(fd, 233.08, 25);
    play(fd, 277.18, 25); play(fd, 155.56, 150); play(fd, 493.88, 25);
    play(fd, 19.44, 125); play(fd, 369.99, 25); play(fd, 391.99, 25);
    play(fd, 19.44, 100); play(fd, 233.08, 25); play(fd, 293.66, 25);
    play(fd, 391.99, 25); play(fd, 19.44, 100); play(fd, 233.08, 25);
    play(fd, 369.99, 25); play(fd, 19.44, 125); play(fd, 138.59, 25);
    play(fd, 155.56, 25); play(fd, 19.44, 100); play(fd, 38.89, 25);
    play(fd, 277.18, 25); play(fd, 19.44, 125); play(fd, 195.99, 25);
    play(fd, 369.99, 25); play(fd, 138.59, 50); play(fd, 19.44, 50);
    play(fd, 195.99, 25); play(fd, 277.18, 25); play(fd, 369.99, 25);
    play(fd, 138.59, 100); play(fd, 155.56, 275); play(fd, 38.89, 25);
    play(fd, 19.44, 25); play(fd, 233.08, 25); play(fd, 349.22, 25);
    play(fd, 391.99, 25); play(fd, 19.44, 100); play(fd, 233.08, 25);
    play(fd, 369.99, 25); play(fd, 38.89, 25); play(fd, 19.44, 100);
    play(fd, 493.88, 25); play(fd, 659.25, 125); play(fd, 19.44, 25);
    play(fd, 391.99, 25); play(fd, 19.44, 100); play(fd, 195.99, 25);
    play(fd, 277.18, 25); play(fd, 369.99, 25); play(fd, 659.25, 100);
    play(fd, 38.89, 25); play(fd, 349.22, 25); play(fd, 783.99, 150);
    play(fd, 739.98, 175); play(fd, 659.25, 125); play(fd, 622.25, 25);
    play(fd, 293.66, 25); play(fd, 587.32, 125); play(fd, 195.99, 25);
    play(fd, 369.99, 25); play(fd, 19.44, 100); play(fd, 38.89, 25);
    play(fd, 659.25, 325); play(fd, 195.99, 25); play(fd, 349.22, 25);
    play(fd, 138.59, 25); play(fd, 116.54, 25); play(fd, 138.59, 25);
    play(fd, 19.44, 25); play(fd, 184.99, 25); play(fd, 293.66, 25);
    play(fd, 116.54, 25); play(fd, 138.59, 25); play(fd, 116.54, 25);
    play(fd, 138.59, 50); play(fd, 116.54, 25); play(fd, 97.99, 300);
    play(fd, 184.99, 25); play(fd, 277.18, 25); play(fd, 587.32, 125);
    play(fd, 195.99, 25); play(fd, 277.18, 25); play(fd, 19.44, 100);
    play(fd, 369.99, 25); play(fd, 587.32, 150); play(fd, 523.25, 175);
    play(fd, 220.00, 25); play(fd, 493.88, 125); play(fd, 195.99, 25);
    play(fd, 277.18, 25); play(fd, 587.32, 125); play(fd, 523.25, 175);
    play(fd, 261.62, 25); play(fd, 19.44, 125); play(fd, 195.99, 25);
    play(fd, 311.12, 25); play(fd, 523.25, 125); play(fd, 277.18, 25);
    play(fd, 311.12, 25); play(fd, 19.44, 100); play(fd, 493.88, 275);
    play(fd, 19.44, 50); play(fd, 184.99, 25); play(fd, 195.99, 25);
    play(fd, 311.12, 25); play(fd, 77.78, 25); play(fd, 97.99, 25);
    play(fd, 19.44, 50); play(fd, 58.27, 25); play(fd, 293.66, 25);
    play(fd, 77.78, 25); play(fd, 493.88, 25); play(fd, 659.25, 25);
    play(fd, 77.78, 25); play(fd, 783.99, 25); play(fd, 987.76, 25);
    play(fd, 77.78, 25); play(fd, 38.89, 25); play(fd, 19.44, 75);
    play(fd, 293.66, 25); play(fd, 311.12, 25); play(fd, 19.44, 100);
    play(fd, 659.25, 25); play(fd, 261.62, 25); play(fd, 369.99, 25);
    play(fd, 659.25, 75); play(fd, 38.89, 25); play(fd, 233.08, 25);
    play(fd, 349.22, 25); play(fd, 391.99, 25); play(fd, 19.44, 100);
    play(fd, 493.88, 25); play(fd, 659.25, 325); play(fd, 369.99, 50);
    play(fd, 19.44, 75); play(fd, 38.89, 25); play(fd, 261.62, 25);
    play(fd, 391.99, 25); play(fd, 783.99, 125); play(fd, 739.98, 175);
    play(fd, 659.25, 150); play(fd, 277.18, 25); play(fd, 369.99, 25);
    play(fd, 587.32, 125); play(fd, 293.66, 25); play(fd, 369.99, 25);
    play(fd, 19.44, 100); play(fd, 116.54, 25); play(fd, 659.25, 300);
    play(fd, 195.99, 25); play(fd, 293.66, 25); play(fd, 116.54, 25);
    play(fd, 138.59, 25); play(fd, 116.54, 50); play(fd, 19.44, 25);
    play(fd, 195.99, 25); play(fd, 311.12, 25); play(fd, 116.54, 50);
    play(fd, 138.59, 25); play(fd, 116.54, 100); play(fd, 19.44, 100);
    play(fd, 311.12, 25); play(fd, 38.89, 25); play(fd, 19.44, 100);
    play(fd, 184.99, 25); play(fd, 277.18, 25); play(fd, 311.12, 25);
    play(fd, 19.44, 100); play(fd, 155.56, 25); play(fd, 293.66, 25);
    play(fd, 739.98, 125); play(fd, 783.99, 175); play(fd, 233.08, 25);
    play(fd, 19.44, 100); play(fd, 38.89, 25); play(fd, 184.99, 25);
    play(fd, 293.66, 25); play(fd, 783.99, 125); play(fd, 195.99, 25);
    play(fd, 293.66, 25); play(fd, 19.44, 100); play(fd, 391.99, 25);
    play(fd, 880.00, 325); play(fd, 220.00, 25); play(fd, 261.62, 25);
    play(fd, 19.44, 100); play(fd, 220.00, 25); play(fd, 261.62, 25);
    play(fd, 19.44, 150); play(fd, 739.98, 200); play(fd, 138.59, 100);
    play(fd, 195.99, 25); play(fd, 277.18, 25); play(fd, 138.59, 25);
    play(fd, 19.44, 75); play(fd, 195.99, 50); play(fd, 369.99, 25);
    play(fd, 783.99, 125); play(fd, 739.98, 150); play(fd, 19.44, 25);
    play(fd, 659.25, 150); play(fd, 293.66, 25); play(fd, 369.99, 25);
    play(fd, 587.32, 100); play(fd, 195.99, 25); play(fd, 349.22, 25);
    play(fd, 739.98, 125); play(fd, 783.99, 175); play(fd, 97.99, 150);
    play(fd, 739.98, 25); play(fd, 277.18, 25); play(fd, 783.99, 125);
    play(fd, 277.18, 25); play(fd, 369.99, 25); play(fd, 19.44, 100);
    play(fd, 739.98, 250); play(fd, 116.54, 100); play(fd, 277.18, 25);
    play(fd, 369.99, 25); play(fd, 116.54, 25); play(fd, 138.59, 25);
    play(fd, 19.44, 50); play(fd, 233.08, 25); play(fd, 277.18, 25);
    play(fd, 369.99, 25); play(fd, 19.44, 75); play(fd, 622.25, 25);
    play(fd, 659.25, 200); play(fd, 155.56, 150); play(fd, 277.18, 25);
    play(fd, 369.99, 25); play(fd, 19.44, 100); play(fd, 233.08, 25);
    play(fd, 369.99, 25); play(fd, 783.99, 125); play(fd, 739.98, 175);
    play(fd, 659.25, 175); play(fd, 369.99, 25); play(fd, 587.32, 125);
    play(fd, 277.18, 25); play(fd, 369.99, 25); play(fd, 739.98, 125);
    play(fd, 783.99, 200); play(fd, 97.99, 125); play(fd, 277.18, 25);
    play(fd, 391.99, 25); play(fd, 783.99, 125); play(fd, 277.18, 25);
    play(fd, 783.99, 25); play(fd, 19.44, 125); play(fd, 739.98, 200);
    play(fd, 116.54, 125); play(fd, 277.18, 25); play(fd, 369.99, 25);
    play(fd, 19.44, 100); play(fd, 261.62, 25); play(fd, 369.99, 25);
    play(fd, 19.44, 150); play(fd, 659.25, 175); play(fd, 155.56, 150);
    play(fd, 369.99, 25); play(fd, 391.99, 25); play(fd, 19.44, 75);
    play(fd, 220.00, 25); play(fd, 277.18, 25); play(fd, 391.99, 25);
    play(fd, 783.99, 125); play(fd, 739.98, 175); play(fd, 659.25, 150);
    play(fd, 195.99, 25); play(fd, 369.99, 25); play(fd, 587.32, 125);
    play(fd, 293.66, 25); play(fd, 369.99, 25); play(fd, 19.44, 100);
    play(fd, 38.89, 25); play(fd, 659.25, 175); play(fd, 38.89, 25);
    play(fd, 116.54, 100); play(fd, 138.59, 25); play(fd, 233.08, 25);
    play(fd, 659.25, 125); play(fd, 195.99, 25); play(fd, 293.66, 25);
    play(fd, 38.89, 25); play(fd, 19.44, 100); play(fd, 659.25, 325);
    play(fd, 195.99, 25); play(fd, 293.66, 25); play(fd, 116.54, 25);
    play(fd, 138.59, 50); play(fd, 116.54, 25); play(fd, 38.89, 25);
    play(fd, 195.99, 25); play(fd, 311.12, 25); play(fd, 116.54, 25);
    play(fd, 138.59, 25); play(fd, 116.54, 50); play(fd, 38.89, 25);
    play(fd, 391.99, 25); play(fd, 19.44, 125); play(fd, 233.08, 50);
    play(fd, 19.44, 100); play(fd, 58.27, 25); play(fd, 220.00, 25);
    play(fd, 311.12, 25); play(fd, 659.25, 100); play(fd, 195.99, 25);
    play(fd, 277.18, 25); play(fd, 349.22, 25); play(fd, 19.44, 100);
    play(fd, 587.32, 200); play(fd, 523.25, 150); play(fd, 277.18, 25);
    play(fd, 493.88, 125); play(fd, 184.99, 25); play(fd, 277.18, 25);
    play(fd, 587.32, 150); play(fd, 554.36, 25); play(fd, 523.25, 150);
    play(fd, 97.99, 75); play(fd, 116.54, 50); play(fd, 220.00, 25);
    play(fd, 261.62, 25); play(fd, 523.25, 125); play(fd, 220.00, 25);
    play(fd, 116.54, 50); play(fd, 97.99, 75); play(fd, 19.44, 50);
    play(fd, 493.88, 175); play(fd, 77.78, 125); play(fd, 369.99, 25);
    play(fd, 391.99, 25); play(fd, 19.44, 75); play(fd, 38.89, 25);
    play(fd, 261.62, 25); play(fd, 369.99, 25); play(fd, 77.78, 125);
    play(fd, 493.88, 25); play(fd, 38.89, 25); play(fd, 19.44, 100);
    play(fd, 369.99, 25); play(fd, 391.99, 25); play(fd, 19.44, 150);
}
