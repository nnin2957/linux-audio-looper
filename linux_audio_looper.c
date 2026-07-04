#include <stdio.h>
#include <alsa/asoundlib.h>
#include <string.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

//non-blocking 모드 설정
void set_nonblocking_input() {
    struct termios ttystate;

    tcgetattr(STDIN_FILENO, &ttystate);
    ttystate.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);

    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
}

//UI
void draw_ui(int track_num, int fr_offset, int total_frames, int rate) {
    double current_time = (double)fr_offset / rate;
    double total_time = (double)total_frames / rate;

    int bar_width = 20;
    double progress = (double)fr_offset / total_frames;
    int filled = (int)(progress * bar_width);

    printf("\033[2J\033[H"); 

    printf("=================================\n");
    printf("     LINUX AUDIO LOOP STATION\n");
    printf("=================================\n\n");

    printf("Current Track : %d\n\n", track_num);

    printf("Loop Progress\n");
    printf("[");
    for (int i = 0; i < bar_width; i++) {
        if (i < filled) printf("#");
        else printf("-");
    }
    printf("] %.1f / %.1f sec\n\n", current_time, total_time);

    printf("Commands\n");
    printf("[r] Next Track\n");
    printf("[c] Clear All\n");
    printf("[q] Quit\n");
    printf("=================================\n");

    fflush(stdout);
}

int main(int argc, char *argv[]) {
    if(argc < 2){
        printf("Usage: %s hw:1,0\n", argv[0]);
        return 1;
    }

    char *device = argv[1];
    snd_pcm_t *capture_handle;
    snd_pcm_t *playback_handle;
    snd_pcm_hw_params_t *hw_params;

    // 48kHz, mono, 16-bit PCM 형식
    unsigned int rate = 48000;
    unsigned int channels = 1; 
    int dir = 0; 
    
    //size 작을수록 지연시간 줄어들지만, underrun/xrun 발생 가능성 커짐. 
    int chunk_size = 64;
    snd_pcm_uframes_t period_size = 64;
    snd_pcm_uframes_t buffer_size = period_size * 2;

    int track_num = 1;
    int running = 1;
    set_nonblocking_input();
    

    snd_pcm_open(&playback_handle, device, SND_PCM_STREAM_PLAYBACK, 0);
    snd_pcm_open(&capture_handle, device, SND_PCM_STREAM_CAPTURE, 0);

    snd_pcm_hw_params_malloc(&hw_params);

    // Playback Setting
    snd_pcm_hw_params_any(playback_handle, hw_params);
    snd_pcm_hw_params_set_access(playback_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(playback_handle, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate_near(playback_handle, hw_params, &rate, &dir);
    snd_pcm_hw_params_set_channels(playback_handle, hw_params, channels); 
    snd_pcm_hw_params_set_period_size_near(
        playback_handle,
        hw_params,
        &period_size,
        &dir
    );

    snd_pcm_hw_params_set_buffer_size_near(
        playback_handle,
        hw_params,
        &buffer_size
    );
    snd_pcm_hw_params(playback_handle, hw_params);

    // Capture Setting
    snd_pcm_hw_params_any(capture_handle, hw_params);
    snd_pcm_hw_params_set_access(capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(capture_handle, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate_near(capture_handle, hw_params, &rate, &dir);
    snd_pcm_hw_params_set_channels(capture_handle, hw_params, channels); 
    snd_pcm_hw_params(capture_handle, hw_params);
    snd_pcm_hw_params_set_period_size_near(
        capture_handle,
        hw_params,
        &period_size,
        &dir
    );

    snd_pcm_hw_params_set_buffer_size_near(
        capture_handle,
        hw_params,
        &buffer_size
    );
    snd_pcm_link(capture_handle, playback_handle);
    snd_pcm_hw_params_free(hw_params);

    snd_pcm_prepare(playback_handle);
    snd_pcm_prepare(capture_handle);
    snd_pcm_start(capture_handle);
    
    int fr_offset = 0;
    int total_frames = 48000 * 5; //5초 길이의 루프 버퍼 생성

    //loopbuf: ALSA에서 한 번에 읽어온 입력 chunk룰 임시로 저장
    short *buffer = (short *)malloc(sizeof(short) * total_frames * channels);
    short *loopbuf = (short *)malloc(sizeof(short) * chunk_size * channels);

    memset(buffer, 0, sizeof(short) * total_frames * channels);

    struct timespec start, end;
    double latency;

    //마이크 입력을 읽어, 기존 루프 버퍼에 더한 뒤, 현재 위치의 오디오를 출력 
    while(running) {

        clock_gettime(CLOCK_MONOTONIC, &start);

        //r: 다음 트랙으로 전환, c: 전체 루프 초기화, q: 프로그램 종료
        char ch;
        if (read(STDIN_FILENO, &ch, 1) > 0) {
            if (ch == 'r') {
                track_num++;
                fr_offset = 0;
            }
            else if (ch == 'c') {
                track_num = 1;
                fr_offset = 0;
                memset(buffer, 0, sizeof(short) * total_frames * channels);
            }
            else if (ch == 'q') {
                running = 0;
                break;
            }
        }

        int r = snd_pcm_readi(capture_handle, loopbuf, 64);
        if (r < 0) {
            snd_pcm_prepare(capture_handle);
            continue;
        }
        //기존 루프 버퍼에 입력된 소리를 더함. (overdubbing 방식)
        for (int i = 0; i < r * channels; i++) {
            int idx = (fr_offset * channels + i) % (total_frames * channels);
            int temp = buffer[idx] + loopbuf[i];

            if (temp > 32767) temp = 32767;
            if (temp < -32768) temp = -32768;

            buffer[idx] = (short)temp;
        }

        //현재 루프 위치의 데이터 출력
        int w = snd_pcm_writei(playback_handle, &buffer[fr_offset * channels], r);
        if (w < 0) {
            snd_pcm_prepare(playback_handle);
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
    
        latency = (end.tv_sec - start.tv_sec) * 1000.0 + 
                (end.tv_nsec - start.tv_nsec) / 1000000.0;

        // 50번 루프마다 UI 출력
        static int count = 0;
        if (count++ % 50 == 0) {
            draw_ui(track_num, fr_offset, total_frames, rate);
        }

        //circular buffer 위치 갱신
        fr_offset = (fr_offset + r) % total_frames;
    }
    snd_pcm_close(capture_handle);
    snd_pcm_close(playback_handle);

    free(loopbuf);
    free(buffer);
    return 0;

}