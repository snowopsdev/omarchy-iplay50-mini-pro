/* Scoped zero-data diagnostic: no mixers, amplifier, capture or global env. */
#include <alsa/asoundlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void mark(const char *fmt, ...) {
    char b[512]; struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    int n = snprintf(b, sizeof b, "PCM_MARK %ld.%09ld ", t.tv_sec, t.tv_nsec);
    va_list ap; va_start(ap, fmt);
    vsnprintf(b+n, sizeof b-n, fmt, ap); va_end(ap);
    size_t len = strlen(b);
    if (len < sizeof b-1) b[len++]='\n';
    (void)write(STDERR_FILENO, b, len);
}
static void check(int r, const char *op) {
    mark("RETURN %s=%d %s", op, r, r<0?snd_strerror(r):"ok");
    if (r<0) _exit(1);
}
#define CALL(op) do {mark("ENTER %s", #op); check((op), #op);} while(0)
int main(int argc, char **argv) {
    if(argc!=3 || strcmp(argv[1],"--run") ||
       (strcmp(argv[2],"drop") && strcmp(argv[2],"drain") && strcmp(argv[2],"safe-drain"))) {
        fprintf(stderr,"Usage: pcm-boundary --run drop|drain|safe-drain (digital zeros only)\n");
        return 2;
    }
    snd_pcm_t *p; snd_pcm_hw_params_t *hw; snd_pcm_sw_params_t *sw;
    snd_pcm_hw_params_alloca(&hw); snd_pcm_sw_params_alloca(&sw);
    mark("BEGIN mode=%s pid=%d", argv[2], getpid());
    const char *device=!strcmp(argv[2],"safe-drain")?"tablet_speaker":"hw:0,0";
    CALL(snd_pcm_open(&p,device,SND_PCM_STREAM_PLAYBACK,0));
    CALL(snd_pcm_hw_params_any(p,hw));
    CALL(snd_pcm_hw_params_set_access(p,hw,SND_PCM_ACCESS_RW_INTERLEAVED));
    CALL(snd_pcm_hw_params_set_format(p,hw,SND_PCM_FORMAT_S16_LE));
    CALL(snd_pcm_hw_params_set_channels(p,hw,2));
    CALL(snd_pcm_hw_params_set_rate(p,hw,48000,0));
    CALL(snd_pcm_hw_params_set_period_size(p,hw,1024,0));
    CALL(snd_pcm_hw_params_set_buffer_size(p,hw,4096));
    CALL(snd_pcm_hw_params(p,hw));
    CALL(snd_pcm_sw_params_current(p,sw));
    snd_pcm_uframes_t boundary;
    CALL(snd_pcm_sw_params_get_boundary(sw,&boundary));
    mark("BOUNDARY %lu",(unsigned long)boundary);
    /* Keep underrun recovery under userspace control for this diagnostic. */
    CALL(snd_pcm_sw_params_set_stop_threshold(p,sw,boundary));
    CALL(snd_pcm_sw_params_set_start_threshold(p,sw,4096));
    CALL(snd_pcm_sw_params_set_avail_min(p,sw,1024));
    CALL(snd_pcm_sw_params(p,sw));
    CALL(snd_pcm_prepare(p));
    short zeros[2048]={0};
    for(int i=0;i<32;i++) {
        mark("ENTER writei[%d]",i);
        snd_pcm_sframes_t n=snd_pcm_writei(p,zeros,1024);
        mark("RETURN writei[%d]=%ld",i,(long)n);
        if(n!=1024) _exit(1);
    }
    if(!strcmp(argv[2],"drop")) CALL(snd_pcm_drop(p));
    else CALL(snd_pcm_drain(p));
    CALL(snd_pcm_hw_free(p));
    CALL(snd_pcm_close(p));
    mark("DONE"); return 0;
}
