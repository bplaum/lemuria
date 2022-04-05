/*****************************************************************
 
  vis_lemuria.c
 
  Copyright (c) 2007 by Burkhard Plaum - plaum@ipf.uni-stuttgart.de
 
  http://gmerlin.sourceforge.net
 
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 
*****************************************************************/

#include <string.h>
#include <math.h>

#include <config.h>
#include <gmerlin/bg_version.h>

#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>
#include <gavl/keycodes.h>

#include <gmerlin/accelerator.h>

#define GL_GLEXT_PROTOTYPES 1

#include <GL/gl.h>


#ifdef HAVE_EGL
#include <EGL/egl.h>
#include <gavl/hw_egl.h>
#else
#include <gavl/hw_glx.h>
#include <GL/glx.h>
#endif


#include <gmerlin/x11/x11.h>

#include <lemuria.h>
#include <gavl/hw_gl.h>


#define LOG_DOMAIN "vis_lemuria"

typedef struct
  {
  lemuria_engine_t * e;
  
  int antialias;

  gavl_audio_format_t afmt;
  gavl_video_format_t vfmt;

  gavl_hw_context_t * hwctx;

  /* Render to texture stuff */
  GLuint frame_buffer;
  GLuint depth_buffer;

  gavl_video_frame_t * frame;

  gavl_audio_sink_t * asink;
  gavl_video_source_t * vsrc;

  bg_controllable_t ctrl;

  bg_accelerator_map_t * accels;
  
  } lemuria_priv_t;



/* Window callbacks */

/* Accelerators */

#define ACCEL_SET_FOREGROUND    2
#define ACCEL_NEXT_FOREGROUND   3
#define ACCEL_SET_BACKGROUND    4
#define ACCEL_NEXT_BACKGROUND   5
#define ACCEL_SET_TEXTURE       6
#define ACCEL_NEXT_TEXTURE      7
#define ACCEL_HELP              8

static const bg_accelerator_t accels[] =
{
  { GAVL_KEY_f,      0,                   ACCEL_NEXT_FOREGROUND   },
  { GAVL_KEY_f,      GAVL_KEY_CONTROL_MASK, ACCEL_SET_FOREGROUND    },
  { GAVL_KEY_w,      0,                   ACCEL_NEXT_BACKGROUND   },
  { GAVL_KEY_w,      GAVL_KEY_CONTROL_MASK, ACCEL_SET_BACKGROUND    },
  { GAVL_KEY_x,      0,                   ACCEL_NEXT_TEXTURE      },
  { GAVL_KEY_x,      GAVL_KEY_CONTROL_MASK, ACCEL_SET_TEXTURE       },
  { GAVL_KEY_F1,     0,                   ACCEL_HELP              },
  { GAVL_KEY_NONE,   0,                0 },
};

static int accel_callback(void * data, int id)
  {
  lemuria_priv_t * vp;
  vp = (lemuria_priv_t *)data;
  switch(id)
    {
    case ACCEL_SET_FOREGROUND:
      lemuria_change_effect(vp->e, LEMURIA_EFFECT_FOREGROUND);
      return 1;
      break;
    case ACCEL_NEXT_FOREGROUND:
      lemuria_next_effect(vp->e, LEMURIA_EFFECT_FOREGROUND);
      return 1;
      break;
    case ACCEL_SET_BACKGROUND:
      lemuria_change_effect(vp->e, LEMURIA_EFFECT_BACKGROUND);
      return 1;
      break;
    case ACCEL_NEXT_BACKGROUND:
      lemuria_next_effect(vp->e, LEMURIA_EFFECT_BACKGROUND);
      return 1;
      break;
    case ACCEL_SET_TEXTURE:
      lemuria_change_effect(vp->e, LEMURIA_EFFECT_TEXTURE);
      return 1;
      break;
    case ACCEL_NEXT_TEXTURE:
      lemuria_next_effect(vp->e, LEMURIA_EFFECT_TEXTURE);
      return 1;
      break;
    case ACCEL_HELP:
      lemuria_print_help(vp->e);
      return 1;
      break;

    }
  return 0;
  }

#if 0

static void set_fullscreen(void * data, int fullscreen)
  {
  lemuria_priv_t * vp;
  vp = (lemuria_priv_t *)data;
  vp->fullscreen = fullscreen;
  }


static void size_changed(void * data, int width, int height)
  {
  lemuria_priv_t * vp;
  vp = (lemuria_priv_t *)data;
  vp->width = width;
  vp->height = height;
  
  if(vp->e)
    lemuria_set_size(vp->e, width, height);
  }

static int motion_callback(void * data, int x, int y, int mask)
  {
  lemuria_priv_t * vp;
  vp = (lemuria_priv_t *)data;

  if(vp->cb && vp->cb->motion_callback)
    {
    vp->cb->motion_callback(vp->cb->data, x, y, mask);
    return 1;
    }
  return 0;
  }

static int button_callback(void * data, int x, int y, int button, int mask)
  {
  lemuria_priv_t * vp;
  vp = (lemuria_priv_t *)data;
  
  if(vp->cb && vp->cb->button_callback)
    {
    vp->cb->button_callback(vp->cb->data, x, y, button, mask);
    return 1;
    }
  return 0;
  }

static int button_release_callback(void * data, int x, int y,
                                   int button, int mask)
  {
  lemuria_priv_t * vp = data;
  
  if(vp->cb && vp->cb->button_release_callback)
    {
    vp->cb->button_release_callback(vp->cb->data, x, y, button, mask);
    return 1;
    }
  return 0;
  }
#endif

static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  lemuria_priv_t * vp = priv;
  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_GUI:
      switch(msg->ID)
        {
        case GAVL_MSG_GUI_KEY_PRESS:
          {
          int accel = 0;
          int key;
          int mask;
          int x;
          int y;
          double pos[2];
          gavl_msg_get_gui_key(msg, &key, &mask, &x, &y, pos);

          fprintf(stderr, "lemuria key pressed\n");

          if(bg_accelerator_map_has_accel(vp->accels, key, mask, &accel))
            {
            accel_callback(vp, accel);
            }
          
          }



          break;
        }
      break;
      
    
    }
  return 1;
  }


static void * create_lemuria()
  {
  lemuria_priv_t * ret;
  ret = calloc(1, sizeof(*ret));

  bg_controllable_init(&ret->ctrl,
                       bg_msg_sink_create(handle_msg, ret, 1),
                       bg_msg_hub_create(1));

  ret->accels = bg_accelerator_map_create();
  bg_accelerator_map_append_array(ret->accels, accels);
  
  return ret;
  }

static void destroy_lemuria(void * priv)
  {
  lemuria_priv_t * vp = priv;

  bg_accelerator_map_destroy(vp->accels);

  bg_controllable_cleanup(&vp->ctrl);

  
  free(vp);
  }

static const bg_parameter_info_t parameters[] =
  {
    {
      .name = "antialias",
      .long_name = "Antialiasing",
      .type = BG_PARAMETER_SLIDER_INT,
      .flags = BG_PARAMETER_SYNC,
      .val_min = GAVL_VALUE_INIT_INT(LEMURIA_ANTIALIAS_NONE),
      .val_max = GAVL_VALUE_INIT_INT(LEMURIA_ANTIALIAS_BEST),
      .help_string = "Antialiasing level (0 = off, highest is best)\n",
    },
    { /* End of parameters */ },
  };

static const bg_parameter_info_t * get_parameters_lemuria(void * priv)
  {
  return parameters;
  }

static void
set_parameter_lemuria(void * priv, const char * name,
                    const gavl_value_t * val)
  {
  lemuria_priv_t * vp;
  vp = (lemuria_priv_t *)priv;
  
  if(!name)
    return;
  if(!strcmp(name, "antialias"))
    {
    if(vp->e)
      lemuria_set_antialiasing(vp->e, val->v.i);
    vp->antialias = val->v.i;
    }
  }

static gavl_source_status_t draw_frame_lemuria(void * priv, gavl_video_frame_t ** frame)
  {
  gavl_gl_frame_info_t * info;

  lemuria_priv_t * vp = priv;

#ifdef HAVE_EGL
  gavl_hw_egl_set_current(vp->hwctx, EGL_NO_SURFACE);
#else
  gavl_hw_glx_set_current(vp->hwctx, None);
#endif
  
  glBindFramebuffer(GL_FRAMEBUFFER, vp->frame_buffer);

  /* Attach texture image */
  info = vp->frame->storage;
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, info->textures[0], 0);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  
  
  lemuria_draw_frame(vp->e);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

#ifdef HAVE_EGL
  gavl_hw_egl_unset_current(vp->hwctx);
#else
  gavl_hw_glx_unset_current(vp->hwctx);
#endif
  *frame = vp->frame;
  
  return GAVL_SOURCE_OK;
  }

static gavl_sink_status_t update_lemuria(void * priv, gavl_audio_frame_t * frame)
  {
  lemuria_priv_t * vp;
  vp = (lemuria_priv_t *)priv;
  lemuria_update_audio(vp->e, (const int16_t**)frame->channels.s_16);
  return GAVL_SINK_OK;

  }

#ifdef HAVE_EGL

static const int attributes[] =
  {
    EGL_RED_SIZE,   8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE,  8,
    EGL_ALPHA_SIZE, 8,
    EGL_NONE
  };

#else

static const int attributes[] =
  {
    GLX_RED_SIZE,   8,
    GLX_GREEN_SIZE, 8,
    GLX_BLUE_SIZE,  8,
    GLX_ALPHA_SIZE, 8,
    GLX_RENDER_TYPE, GLX_RGBA_BIT,
    GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
    GLX_DOUBLEBUFFER, True,
    None
  };
#endif

static int
open_lemuria(void * priv, gavl_audio_format_t * audio_format,
             gavl_video_format_t * video_format)
  {
  lemuria_priv_t * vp;

  vp = priv;
  
  audio_format->interleave_mode = GAVL_INTERLEAVE_NONE;
  audio_format->num_channels = 2;
  audio_format->channel_locations[0] = GAVL_CHID_NONE;
  gavl_set_channel_setup(audio_format);
  audio_format->sample_format = GAVL_SAMPLE_S16;
  audio_format->samples_per_frame = LEMURIA_TIME_SAMPLES;

  gavl_audio_format_copy(&vp->afmt, audio_format);

#ifdef HAVE_EGL
  vp->hwctx = gavl_hw_ctx_create_egl(attributes, GAVL_HW_EGL_GL_X11, NULL);
#else
  vp->hwctx = gavl_hw_ctx_create_glx(NULL, attributes);
#endif

  /* Adjust video format */
  video_format->pixelformat    = GAVL_RGBA_32;
  video_format->hwctx = vp->hwctx;
  video_format->pixel_width = 1;
  video_format->pixel_height = 1;
  
  gavl_video_format_set_frame_size(video_format, 1, 1);

  gavl_video_format_copy(&vp->vfmt, video_format);
  
  vp->frame = gavl_hw_video_frame_create_hw(vp->hwctx, &vp->vfmt);

#ifdef HAVE_EGL
  gavl_hw_egl_set_current(vp->hwctx, EGL_NO_SURFACE);
#else
  gavl_hw_glx_set_current(vp->hwctx, None);
 
#endif
  
  /* Generate frame buffer */
  glGenFramebuffers(1, &vp->frame_buffer);
  glBindFramebuffer(GL_FRAMEBUFFER, vp->frame_buffer);

  /* Create and attach depth buffer */
  glGenRenderbuffers(1, &vp->depth_buffer);
  glBindRenderbuffer(GL_RENDERBUFFER, vp->depth_buffer);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, vp->vfmt.image_width, vp->vfmt.image_height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, vp->depth_buffer);
  
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Created framebuffer");
  else
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Creating framebuffer failed");
  
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  
  
  vp->e = lemuria_create();

  lemuria_set_antialiasing(vp->e, LEMURIA_ANTIALIAS_BEST);
  
  //  lemuria_set_antialiasing(vp->e, vp->antialias);
  
  lemuria_set_size(vp->e, vp->vfmt.image_width, vp->vfmt.image_height);

#ifdef HAVE_EGL
  gavl_hw_egl_unset_current(vp->hwctx);
#else
  gavl_hw_glx_unset_current(vp->hwctx);
#endif  
  
  vp->asink = gavl_audio_sink_create(NULL, update_lemuria, vp, &vp->afmt);
  vp->vsrc = gavl_video_source_create(draw_frame_lemuria,
                                         vp, GAVL_SOURCE_SRC_ALLOC,
                                         &vp->vfmt);


  
  return 1;
  }



static void close_lemuria(void * priv)
  {
  lemuria_priv_t * vp;
  vp = priv;
  
  bg_controllable_cleanup(&vp->ctrl);
  
  if(vp->e)
    {
#ifdef HAVE_EGL
    gavl_hw_egl_set_current(vp->hwctx, EGL_NO_SURFACE);
#else
    gavl_hw_glx_set_current(vp->hwctx, None);
#endif
    lemuria_destroy(vp->e);
#ifdef HAVE_EGL
    gavl_hw_egl_unset_current(vp->hwctx);
#else
    gavl_hw_glx_unset_current(vp->hwctx);
#endif
    }

  if(vp->asink)
    {
    gavl_audio_sink_destroy(vp->asink);
    vp->asink = NULL;
    }
    
  if(vp->vsrc)
    {
    gavl_video_source_destroy(vp->vsrc);
    vp->vsrc = NULL;
    }

  if(vp->frame)
    {
    gavl_video_frame_destroy(vp->frame);
    vp->frame = NULL;
    }


  }

static gavl_video_source_t * get_source_lemuria(void * priv)
  {
  lemuria_priv_t * vp;
  vp = priv;
  return vp->vsrc;
  }

static gavl_audio_sink_t * get_sink_lemuria(void * priv)
  {
  lemuria_priv_t * vp;
  vp = priv;
  return vp->asink;
  }

static bg_controllable_t * get_controllable_lemuria(void * priv)
  {
  lemuria_priv_t * vp;
  vp = priv;
  return &vp->ctrl;
  }

const bg_visualization_plugin_t the_plugin = 
  {
    .common =
    {
      BG_LOCALE,
      .name =      "vis_lemuria",
      .long_name = TRS("Lemuria"),
      .description = TRS("OpenGL visualization with many effects"),
      .type =     BG_PLUGIN_VISUALIZATION,
      .flags =    0,
      .create =   create_lemuria,
      .destroy =   destroy_lemuria,
      .get_parameters =   get_parameters_lemuria,
      .set_parameter =    set_parameter_lemuria,
      .get_controllable =   get_controllable_lemuria,
      .priority =         1,
    },
    .open = open_lemuria,
    .get_source = get_source_lemuria,
    .get_sink = get_sink_lemuria,
    
    .close = close_lemuria
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
