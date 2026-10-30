/* A short wl_shm compositor benchmark. No input devices are bound.
 * Three pre-painted buffers avoid measuring per-frame client rasterization. */
#define _GNU_SOURCE
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm;
static struct wl_surface *surface;
static struct xdg_surface *xs;
static struct wl_callback *frame;
static struct wl_buffer *buffers[3];
static int busy[3], width=400, height=400, ready, done, next, frames, small;
static double started;
static double now(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void draw(void);
static void release(void *data, struct wl_buffer *buffer) {
    (void)buffer;
    busy[(intptr_t)data]=0;
    if (ready && !frame && !done) draw();
}
static const struct wl_buffer_listener buffer_listener={release};
static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
    (void)data;(void)time;wl_callback_destroy(cb);frame=NULL;frames++;
    if (now()-started>=4.0) {done=1;return;}
    draw();
}
static const struct wl_callback_listener frame_listener={frame_done};
static void draw(void) {
    if (frame || done) return;
    for (int n=0;n<3;n++) {
        int i=(next+n)%3;
        if (busy[i]) continue;
        next=(i+1)%3;busy[i]=1;
        frame=wl_surface_frame(surface);
        wl_callback_add_listener(frame,&frame_listener,NULL);
        wl_surface_attach(surface,buffers[i],0,0);
        if (small) wl_surface_damage(surface,0,0,100,80);
        else wl_surface_damage(surface,0,0,width,height);
        wl_surface_commit(surface);return;
    }
}
static void configure(void *data, struct xdg_surface *s, uint32_t serial) {
    (void)data;xdg_surface_ack_configure(s,serial);
    if (ready) return;
    size_t size=(size_t)width*height*4;
    int fd=memfd_create("tablet-framebench",MFD_CLOEXEC);
    if(fd<0 || ftruncate(fd,size*3)) exit(2);
    uint32_t *pixels=mmap(NULL,size*3,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(pixels==MAP_FAILED) exit(2);
    struct wl_shm_pool *pool=wl_shm_create_pool(shm,fd,size*3);
    for(int i=0;i<3;i++) {
        uint32_t *p=pixels+(size/4)*i;
        for(int y=0;y<height;y++) for(int x=0;x<width;x++)
            p[y*width+x]=(y>=20 && y<60 && x>=20+i*20 && x<40+i*20)?0xff8199b0:0xff202830;
        buffers[i]=wl_shm_pool_create_buffer(pool,size*i,width,height,width*4,WL_SHM_FORMAT_XRGB8888);
        wl_buffer_add_listener(buffers[i],&buffer_listener,(void*)(intptr_t)i);
    }
    wl_shm_pool_destroy(pool);close(fd);ready=1;started=now();draw();
}
static const struct xdg_surface_listener xs_listener={configure};
static void top_configure(void *d,struct xdg_toplevel *t,int32_t w,int32_t h,struct wl_array *a) {
    (void)d;(void)t;(void)a;if(!ready && w>0 && h>0){width=w;height=h;}
}
static void top_close(void *d,struct xdg_toplevel *t){(void)d;(void)t;done=1;}
static const struct xdg_toplevel_listener top_listener={.configure=top_configure,.close=top_close};
static void ping(void *d,struct xdg_wm_base *b,uint32_t serial){(void)d;xdg_wm_base_pong(b,serial);}
static const struct xdg_wm_base_listener wm_listener={ping};
static void global(void *d,struct wl_registry *r,uint32_t name,const char *interface,uint32_t version) {
    (void)d;(void)version;
    if(!strcmp(interface,"wl_compositor"))compositor=wl_registry_bind(r,name,&wl_compositor_interface,1);
    else if(!strcmp(interface,"wl_shm"))shm=wl_registry_bind(r,name,&wl_shm_interface,1);
    else if(!strcmp(interface,"xdg_wm_base")){wm=wl_registry_bind(r,name,&xdg_wm_base_interface,1);xdg_wm_base_add_listener(wm,&wm_listener,NULL);}
}
static void removed(void *d,struct wl_registry *r,uint32_t name){(void)d;(void)r;(void)name;}
static const struct wl_registry_listener registry_listener={global,removed};
int main(int argc,char **argv) {
    small=argc>1 && !strcmp(argv[1],"small");
    display=wl_display_connect(NULL);if(!display)return 2;
    struct wl_registry *r=wl_display_get_registry(display);
    wl_registry_add_listener(r,&registry_listener,NULL);wl_display_roundtrip(display);
    if(!compositor || !shm || !wm)return 2;
    surface=wl_compositor_create_surface(compositor);
    xs=xdg_wm_base_get_xdg_surface(wm,surface);xdg_surface_add_listener(xs,&xs_listener,NULL);
    struct xdg_toplevel *top=xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(top,&top_listener,NULL);
    xdg_toplevel_set_min_size(top,400,400);
    xdg_toplevel_set_max_size(top,400,400);
    xdg_toplevel_set_app_id(top,"tablet-framebench");xdg_toplevel_set_title(top,"Tablet redraw test");
    wl_surface_commit(surface);
    while(!done && wl_display_dispatch(display)!=-1){}
    printf("{\"frames\":%d,\"seconds\":%.3f,\"fps\":%.2f,\"width\":%d,\"height\":%d,\"small_damage\":%d}\n",frames,now()-started,frames/(now()-started),width,height,small);
    wl_display_disconnect(display);return 0;
}
