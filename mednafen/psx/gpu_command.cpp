#include "psx.h"
#include "../../rsx/rsx_intf.h"

#include "../pgxp/pgxp_main.h"
#include "../pgxp/pgxp_gpu.h"

#include "gpu_common.h"

extern PS_GPU GPU;

void SetTPage(PS_GPU *gpu, const uint32_t cmdw)
{
   const unsigned NewTexPageX = (cmdw & 0xF) * 64;
   const unsigned NewTexPageY = (cmdw & 0x10) * 16;
   const unsigned NewTexMode  = (cmdw >> 7) & 0x3;

   gpu->abr = (cmdw >> 5) & 0x3;

   if(!NewTexMode != !gpu->TexMode || NewTexPageX != gpu->TexPageX || NewTexPageY != gpu->TexPageY)
      InvalidateTexCache(gpu);

   if(gpu->TexDisableAllowChange)
   {
      bool NewTexDisable = (cmdw >> 11) & 1;

      if (NewTexDisable != gpu->TexDisable)
         InvalidateTexCache(gpu);

      gpu->TexDisable = NewTexDisable;
   }

   gpu->TexPageX = NewTexPageX;
   gpu->TexPageY = NewTexPageY;
   gpu->TexMode  = NewTexMode;

   RecalcTexWindowStuff(gpu);
}

static void Command_ClearCache(PS_GPU* g, const uint32 *cb)
{
   InvalidateCache(g);
}

// Special RAM write mode(16 pixels at a time),
// does *not* appear to use mask drawing environment settings.
static void Command_FBFill(PS_GPU* gpu, const uint32 *cb)
{
   unsigned y;
   int32_t r                 = cb[0] & 0xFF;
   int32_t g                 = (cb[0] >> 8) & 0xFF;
   int32_t b                 = (cb[0] >> 16) & 0xFF;
   const uint16_t fill_value = ((r >> 3) << 0) | ((g >> 3) << 5) | ((b >> 3) << 10);
   int32_t destX             = (cb[1] >>  0) & 0x3F0;
   int32_t destY             = (cb[1] >> 16) & 0x3FF;
   int32_t width             = (((cb[2] >> 0) & 0x3FF) + 0xF) & ~0xF;
   int32_t height            = (cb[2] >> 16) & 0x1FF;

   //printf("[GPU] FB Fill %d:%d w=%d, h=%d\n", destX, destY, width, height);
   gpu->DrawTimeAvail       -= 46; // Approximate

   for(y = 0; y < height; y++)
   {
      unsigned x;
      const int32 d_y = (y + destY) & 511;

      if(LineSkipTest(gpu, d_y))
         continue;

      gpu->DrawTimeAvail -= (width >> 3) + 9;

      for(x = 0; x < width; x++)
      {
         const int32 d_x = (x + destX) & 1023;

         texel_put(d_x, d_y, fill_value);
      }
   }

   rsx_intf_fill_rect(cb[0], destX, destY, width, height);
}

void Command_FBCopy(PS_GPU* g, const uint32 *cb)
{
   unsigned y;
   int32_t sourceX = (cb[1] >> 0) & 0x3FF;
   int32_t sourceY = (cb[1] >> 16) & 0x3FF;
   int32_t destX   = (cb[2] >> 0) & 0x3FF;
   int32_t destY   = (cb[2] >> 16) & 0x3FF;
   int32_t width   = (cb[3] >> 0) & 0x3FF;
   int32_t height  = (cb[3] >> 16) & 0x1FF;

   if(!width)
      width = 0x400;

   if(!height)
      height = 0x200;

   InvalidateTexCache(g);
   //printf("FB Copy: %d %d %d %d %d %d\n", sourceX, sourceY, destX, destY, width, height);

   g->DrawTimeAvail -= (width * height) * 2;

   for(y = 0; y < height; y++)
   {
      unsigned x;

      for(x = 0; x < width; x += 128)
      {
         const int32 chunk_x_max = std::min<int32>(width - x, 128);
         uint16 tmpbuf[128]; // TODO: Check and see if the GPU is actually (ab)using the CLUT or texture cache.

         for(int32 chunk_x = 0; chunk_x < chunk_x_max; chunk_x++)
         {
            int32 s_y = (y + sourceY) & 511;
            int32 s_x = (x + chunk_x + sourceX) & 1023;

            // XXX make upscaling-friendly, as it is we copy at 1x
            tmpbuf[chunk_x] = texel_fetch(g, s_x, s_y);
         }

         for(int32 chunk_x = 0; chunk_x < chunk_x_max; chunk_x++)
         {
            int32 d_y = (y + destY) & 511;
            int32 d_x = (x + chunk_x + destX) & 1023;

            if(!(texel_fetch(g, d_x, d_y) & g->MaskEvalAND))
               texel_put(d_x, d_y, tmpbuf[chunk_x] | g->MaskSetOR);
         }
      }
   }

   rsx_intf_copy_rect(sourceX, sourceY, destX, destY, width, height, g->MaskEvalAND, g->MaskSetOR);
}

void Command_FBWrite(PS_GPU* g, const uint32 *cb)
{
   //assert(InCmd == INCMD_NONE);

   g->FBRW_X = (cb[1] >>  0) & 0x3FF;
   g->FBRW_Y = (cb[1] >> 16) & 0x3FF;

   g->FBRW_W = (cb[2] >>  0) & 0x3FF;
   g->FBRW_H = (cb[2] >> 16) & 0x1FF;

   if(!g->FBRW_W)
      g->FBRW_W = 0x400;

   if(!g->FBRW_H)
      g->FBRW_H = 0x200;

   g->FBRW_CurX = g->FBRW_X;
   g->FBRW_CurY = g->FBRW_Y;

   InvalidateTexCache(g);

   if(g->FBRW_W != 0 && g->FBRW_H != 0)
      g->InCmd = INCMD_FBWRITE;
}

/* FBRead: PS1 GPU in SCPH-5501 gives odd, inconsistent results when
 * raw_height == 0, or raw_height != 0x200 && (raw_height & 0x1FF) == 0
 */

void Command_FBRead(PS_GPU* g, const uint32 *cb)
{
   //assert(g->InCmd == INCMD_NONE);

   g->FBRW_X = (cb[1] >>  0) & 0x3FF;
   g->FBRW_Y = (cb[1] >> 16) & 0x3FF;

   g->FBRW_W = (cb[2] >>  0) & 0x3FF;
   g->FBRW_H = (cb[2] >> 16) & 0x3FF;

   if(!g->FBRW_W)
      g->FBRW_W = 0x400;

   if(g->FBRW_H > 0x200)
      g->FBRW_H &= 0x1FF;

   g->FBRW_CurX = g->FBRW_X;
   g->FBRW_CurY = g->FBRW_Y;

   InvalidateTexCache(g);

   if(g->FBRW_W != 0 && g->FBRW_H != 0)
      g->InCmd = INCMD_FBREAD;

   if (!rsx_intf_has_software_renderer())
   {
	   //fprintf(stderr, "Hard GPU readback (X: %d, Y: %d, W: %d, H: %d)\n", g->FBRW_X, g->FBRW_Y, g->FBRW_W, g->FBRW_H);
       /* Need a hard readback from GPU renderer. */
       bool supported = rsx_intf_read_vram(
               g->FBRW_X, g->FBRW_Y,
               g->FBRW_W, g->FBRW_H,
               g->vram);

       //if (!supported)
       //    fprintf(stderr, "Game is trying to reading back from VRAM, but SW rendering is not enabled, and RSX backend does not support it.\n");
   }
}

static void Command_IRQ(PS_GPU* g, const uint32 *cb)
{
   g->IRQPending = true;
   IRQ_Assert(IRQ_GPU, g->IRQPending);
}

static void Command_DrawMode(PS_GPU* g, const uint32 *cb)
{
   const uint32 cmdw = *cb;

   SetTPage(g, cmdw);

   g->SpriteFlip = (cmdw & 0x3000);
   g->dtd =        (cmdw >> 9) & 1;
   g->dfe =        (cmdw >> 10) & 1;

   if (g->dfe)
   {
      GPU.display_possibly_dirty = true;
      //printf("Display possibly dirty this frame\n");
   }

   //printf("*******************DFE: %d -- scanline=%d\n", dfe, scanline);
}

static void Command_TexWindow(PS_GPU* g, const uint32 *cb)
{
   g->tww = (*cb & 0x1F);
   g->twh = ((*cb >> 5) & 0x1F);
   g->twx = ((*cb >> 10) & 0x1F);
   g->twy = ((*cb >> 15) & 0x1F);

   RecalcTexWindowStuff(g);
   rsx_intf_set_tex_window(g->tww, g->twh, g->twx, g->twy);
}

static void Command_Clip0(PS_GPU* g, const uint32 *cb)
{
   g->ClipX0 = *cb & 1023;
   g->ClipY0 = (*cb >> 10) & 1023;
   rsx_intf_set_draw_area(g->ClipX0, g->ClipY0,
           g->ClipX1, g->ClipY1);
}

static void Command_Clip1(PS_GPU* g, const uint32 *cb)
{
   g->ClipX1 = *cb & 1023;
   g->ClipY1 = (*cb >> 10) & 1023;
   rsx_intf_set_draw_area(g->ClipX0, g->ClipY0,
         g->ClipX1, g->ClipY1);
}

static void Command_DrawingOffset(PS_GPU* g, const uint32 *cb)
{
   g->OffsX = sign_x_to_s32(11, (*cb & 2047));
   g->OffsY = sign_x_to_s32(11, ((*cb >> 11) & 2047));

   //fprintf(stderr, "[GPU] Drawing offset: %d(raw=%d) %d(raw=%d) -- %d\n", OffsX, *cb, OffsY, *cb >> 11, scanline);
}

static void Command_MaskSetting(PS_GPU* g, const uint32 *cb)
{
   //printf("Mask setting: %08x\n", *cb);
   g->MaskSetOR   = (*cb & 1) ? 0x8000 : 0x0000;
   g->MaskEvalAND = (*cb & 2) ? 0x8000 : 0x0000;

   rsx_intf_set_mask_setting(g->MaskSetOR, g->MaskEvalAND);
}

extern CTEntry GPU_Commands_Polygon_0x20;
extern CTEntry GPU_Commands_Polygon_0x21;
extern CTEntry GPU_Commands_Polygon_0x22;
extern CTEntry GPU_Commands_Polygon_0x23;
extern CTEntry GPU_Commands_Polygon_0x24;
extern CTEntry GPU_Commands_Polygon_0x25;
extern CTEntry GPU_Commands_Polygon_0x26;
extern CTEntry GPU_Commands_Polygon_0x27;
extern CTEntry GPU_Commands_Polygon_0x28;
extern CTEntry GPU_Commands_Polygon_0x29;
extern CTEntry GPU_Commands_Polygon_0x2a;
extern CTEntry GPU_Commands_Polygon_0x2b;
extern CTEntry GPU_Commands_Polygon_0x2c;
extern CTEntry GPU_Commands_Polygon_0x2d;
extern CTEntry GPU_Commands_Polygon_0x2e;
extern CTEntry GPU_Commands_Polygon_0x2f;
extern CTEntry GPU_Commands_Polygon_0x30;
extern CTEntry GPU_Commands_Polygon_0x31;
extern CTEntry GPU_Commands_Polygon_0x32;
extern CTEntry GPU_Commands_Polygon_0x33;
extern CTEntry GPU_Commands_Polygon_0x34;
extern CTEntry GPU_Commands_Polygon_0x35;
extern CTEntry GPU_Commands_Polygon_0x36;
extern CTEntry GPU_Commands_Polygon_0x37;
extern CTEntry GPU_Commands_Polygon_0x38;
extern CTEntry GPU_Commands_Polygon_0x39;
extern CTEntry GPU_Commands_Polygon_0x3a;
extern CTEntry GPU_Commands_Polygon_0x3b;
extern CTEntry GPU_Commands_Polygon_0x3c;
extern CTEntry GPU_Commands_Polygon_0x3d;
extern CTEntry GPU_Commands_Polygon_0x3e;
extern CTEntry GPU_Commands_Polygon_0x3f;

extern CTEntry GPU_Commands_Line_0x40;
extern CTEntry GPU_Commands_Line_0x41;
extern CTEntry GPU_Commands_Line_0x42;
extern CTEntry GPU_Commands_Line_0x43;
extern CTEntry GPU_Commands_Line_0x44;
extern CTEntry GPU_Commands_Line_0x45;
extern CTEntry GPU_Commands_Line_0x46;
extern CTEntry GPU_Commands_Line_0x47;
extern CTEntry GPU_Commands_Line_0x48;
extern CTEntry GPU_Commands_Line_0x49;
extern CTEntry GPU_Commands_Line_0x4a;
extern CTEntry GPU_Commands_Line_0x4b;
extern CTEntry GPU_Commands_Line_0x4c;
extern CTEntry GPU_Commands_Line_0x4d;
extern CTEntry GPU_Commands_Line_0x4e;
extern CTEntry GPU_Commands_Line_0x4f;
extern CTEntry GPU_Commands_Line_0x50;
extern CTEntry GPU_Commands_Line_0x51;
extern CTEntry GPU_Commands_Line_0x52;
extern CTEntry GPU_Commands_Line_0x53;
extern CTEntry GPU_Commands_Line_0x54;
extern CTEntry GPU_Commands_Line_0x55;
extern CTEntry GPU_Commands_Line_0x56;
extern CTEntry GPU_Commands_Line_0x57;
extern CTEntry GPU_Commands_Line_0x58;
extern CTEntry GPU_Commands_Line_0x59;
extern CTEntry GPU_Commands_Line_0x5a;
extern CTEntry GPU_Commands_Line_0x5b;
extern CTEntry GPU_Commands_Line_0x5c;
extern CTEntry GPU_Commands_Line_0x5d;
extern CTEntry GPU_Commands_Line_0x5e;
extern CTEntry GPU_Commands_Line_0x5f;

extern CTEntry GPU_Commands_Sprite_0x60;
extern CTEntry GPU_Commands_Sprite_0x61;
extern CTEntry GPU_Commands_Sprite_0x62;
extern CTEntry GPU_Commands_Sprite_0x63;
extern CTEntry GPU_Commands_Sprite_0x64;
extern CTEntry GPU_Commands_Sprite_0x65;
extern CTEntry GPU_Commands_Sprite_0x66;
extern CTEntry GPU_Commands_Sprite_0x67;
extern CTEntry GPU_Commands_Sprite_0x68;
extern CTEntry GPU_Commands_Sprite_0x69;
extern CTEntry GPU_Commands_Sprite_0x6a;
extern CTEntry GPU_Commands_Sprite_0x6b;
extern CTEntry GPU_Commands_Sprite_0x6c;
extern CTEntry GPU_Commands_Sprite_0x6d;
extern CTEntry GPU_Commands_Sprite_0x6e;
extern CTEntry GPU_Commands_Sprite_0x6f;
extern CTEntry GPU_Commands_Sprite_0x70;
extern CTEntry GPU_Commands_Sprite_0x71;
extern CTEntry GPU_Commands_Sprite_0x72;
extern CTEntry GPU_Commands_Sprite_0x73;
extern CTEntry GPU_Commands_Sprite_0x74;
extern CTEntry GPU_Commands_Sprite_0x75;
extern CTEntry GPU_Commands_Sprite_0x76;
extern CTEntry GPU_Commands_Sprite_0x77;
extern CTEntry GPU_Commands_Sprite_0x78;
extern CTEntry GPU_Commands_Sprite_0x79;
extern CTEntry GPU_Commands_Sprite_0x7a;
extern CTEntry GPU_Commands_Sprite_0x7b;
extern CTEntry GPU_Commands_Sprite_0x7c;
extern CTEntry GPU_Commands_Sprite_0x7d;
extern CTEntry GPU_Commands_Sprite_0x7e;
extern CTEntry GPU_Commands_Sprite_0x7f;

CTEntry GPU_Commands[256] =
{
   /* 0x00 */
   NULLCMD(),
   OTHER_HELPER(1, 2, false, Command_ClearCache),
   OTHER_HELPER(3, 3, false, Command_FBFill),

   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),
   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),

   /* 0x10 */
   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),
   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),

   OTHER_HELPER(1, 1, false,  Command_IRQ),

   /* 0x20 */
   GPU_Commands_Polygon_0x20,
   GPU_Commands_Polygon_0x21,
   GPU_Commands_Polygon_0x22,
   GPU_Commands_Polygon_0x23,
   GPU_Commands_Polygon_0x24,
   GPU_Commands_Polygon_0x25,
   GPU_Commands_Polygon_0x26,
   GPU_Commands_Polygon_0x27,
   GPU_Commands_Polygon_0x28,
   GPU_Commands_Polygon_0x29,
   GPU_Commands_Polygon_0x2a,
   GPU_Commands_Polygon_0x2b,
   GPU_Commands_Polygon_0x2c,
   GPU_Commands_Polygon_0x2d,
   GPU_Commands_Polygon_0x2e,
   GPU_Commands_Polygon_0x2f,
   GPU_Commands_Polygon_0x30,
   GPU_Commands_Polygon_0x31,
   GPU_Commands_Polygon_0x32,
   GPU_Commands_Polygon_0x33,
   GPU_Commands_Polygon_0x34,
   GPU_Commands_Polygon_0x35,
   GPU_Commands_Polygon_0x36,
   GPU_Commands_Polygon_0x37,
   GPU_Commands_Polygon_0x38,
   GPU_Commands_Polygon_0x39,
   GPU_Commands_Polygon_0x3a,
   GPU_Commands_Polygon_0x3b,
   GPU_Commands_Polygon_0x3c,
   GPU_Commands_Polygon_0x3d,
   GPU_Commands_Polygon_0x3e,
   GPU_Commands_Polygon_0x3f,

   GPU_Commands_Line_0x40,
   GPU_Commands_Line_0x41,
   GPU_Commands_Line_0x42,
   GPU_Commands_Line_0x43,
   GPU_Commands_Line_0x44,
   GPU_Commands_Line_0x45,
   GPU_Commands_Line_0x46,
   GPU_Commands_Line_0x47,
   GPU_Commands_Line_0x48,
   GPU_Commands_Line_0x49,
   GPU_Commands_Line_0x4a,
   GPU_Commands_Line_0x4b,
   GPU_Commands_Line_0x4c,
   GPU_Commands_Line_0x4d,
   GPU_Commands_Line_0x4e,
   GPU_Commands_Line_0x4f,
   GPU_Commands_Line_0x50,
   GPU_Commands_Line_0x51,
   GPU_Commands_Line_0x52,
   GPU_Commands_Line_0x53,
   GPU_Commands_Line_0x54,
   GPU_Commands_Line_0x55,
   GPU_Commands_Line_0x56,
   GPU_Commands_Line_0x57,
   GPU_Commands_Line_0x58,
   GPU_Commands_Line_0x59,
   GPU_Commands_Line_0x5a,
   GPU_Commands_Line_0x5b,
   GPU_Commands_Line_0x5c,
   GPU_Commands_Line_0x5d,
   GPU_Commands_Line_0x5e,
   GPU_Commands_Line_0x5f,

   GPU_Commands_Sprite_0x60,
   GPU_Commands_Sprite_0x61,
   GPU_Commands_Sprite_0x62,
   GPU_Commands_Sprite_0x63,
   GPU_Commands_Sprite_0x64,
   GPU_Commands_Sprite_0x65,
   GPU_Commands_Sprite_0x66,
   GPU_Commands_Sprite_0x67,
   GPU_Commands_Sprite_0x68,
   GPU_Commands_Sprite_0x69,
   GPU_Commands_Sprite_0x6a,
   GPU_Commands_Sprite_0x6b,
   GPU_Commands_Sprite_0x6c,
   GPU_Commands_Sprite_0x6d,
   GPU_Commands_Sprite_0x6e,
   GPU_Commands_Sprite_0x6f,
   GPU_Commands_Sprite_0x70,
   GPU_Commands_Sprite_0x71,
   GPU_Commands_Sprite_0x72,
   GPU_Commands_Sprite_0x73,
   GPU_Commands_Sprite_0x74,
   GPU_Commands_Sprite_0x75,
   GPU_Commands_Sprite_0x76,
   GPU_Commands_Sprite_0x77,
   GPU_Commands_Sprite_0x78,
   GPU_Commands_Sprite_0x79,
   GPU_Commands_Sprite_0x7a,
   GPU_Commands_Sprite_0x7b,
   GPU_Commands_Sprite_0x7c,
   GPU_Commands_Sprite_0x7d,
   GPU_Commands_Sprite_0x7e,
   GPU_Commands_Sprite_0x7f,

   /* 0x80 ... 0x9F */
   OTHER_HELPER_X32(4, 2, false, Command_FBCopy),

   /* 0xA0 ... 0xBF */
   OTHER_HELPER_X32(3, 2, false, Command_FBWrite),

   /* 0xC0 ... 0xDF */
   OTHER_HELPER_X32(3, 2, false, Command_FBRead),

   /* 0xE0 */

   NULLCMD(),
   OTHER_HELPER(1, 2, false, Command_DrawMode),
   OTHER_HELPER(1, 2, false, Command_TexWindow),
   OTHER_HELPER(1, 1, true,  Command_Clip0),
   OTHER_HELPER(1, 1, true,  Command_Clip1),
   OTHER_HELPER(1, 1, true,  Command_DrawingOffset),
   OTHER_HELPER(1, 2, false, Command_MaskSetting),

   NULLCMD(),
   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),

   /* 0xF0 */
   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),
   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),

};
