#pragma once

#include "../atlas/atlas.hpp"
#include "../vulkan/device.hpp"
#include "../vulkan/vulkan.hpp"
#include "../custom-textures/texture_tracker.hpp"

#ifdef VULKAN_WSI
#include "wsi.hpp"
#endif

#include <string.h>

namespace PSX
{

struct Vertex
{
	float x, y, w;
	uint32_t color;
	uint16_t u, v;
};

struct TextureWindow
{
	uint8_t mask_x, mask_y, or_x, or_y;
};

struct UVRect
{
	uint16_t min_u, min_v, max_u, max_v;
};

enum class SemiTransparentMode
{
	None,
	Average,
	Add,
	Sub,
	AddQuarter
};

class Renderer : private HazardListener, private TextureUploader
{
public:
	enum class ScanoutMode
	{
		// Use extra precision bits.
		ABGR1555_555,
		// Use extra precision bits to dither down to a native ABGR1555 image.
		// The dither happens in the wrong place, but should be "good" enough to feel authentic.
		ABGR1555_Dither,
		// MDEC
		BGR24
	};

	enum class ScanoutFilter
	{
		None,
		SSAA,
		MDEC_YUV
	};

	enum class WidthMode
	{
		WIDTH_MODE_256 = 0,
		WIDTH_MODE_320 = 1,
		WIDTH_MODE_512 = 2,
		WIDTH_MODE_640 = 3,
		WIDTH_MODE_368 = 4
	};

	struct DisplayRect
	{
		// Unlike Rect, the x-y coordinates for a DisplayRect can be negative
		int x = 0;
		int y = 0;
		unsigned width = 0;
		unsigned height = 0;

		DisplayRect() = default;
		DisplayRect(int x, int y, unsigned width, unsigned height)
		    : x(x)
		    , y(y)
		    , width(width)
		    , height(height)
		{
		}
	};

	enum class PrimitiveType
	{
		Polygon,
		Polygon_w1,
		Sprite,
	};

	struct RenderState
	{
		//Rect display_mode;
		Rect display_fb_rect;
		bool valid_fb_rect = false;
		DisplayRect disp_output_rect;
		bool valid_output_rect = false;
		Rect last_fb_rect;
		TextureWindow texture_window;
		Rect cached_window_rect;
		Rect draw_rect;
		int draw_offset_x = 0;
		int draw_offset_y = 0;
		unsigned palette_offset_x = 0;
		unsigned palette_offset_y = 0;
		unsigned texture_offset_x = 0;
		unsigned texture_offset_y = 0;

		int vert_start = 0x10;
		int vert_end = 0x100;
		int horiz_start = 0x200;
		int horiz_end = 0xC00;

		bool is_pal = false;
		bool is_480i = false;
		WidthMode width_mode = WidthMode::WIDTH_MODE_320;
		bool crop_overscan = false;

		// Experimental horizontal offset feature
		int offset_cycles = 0;

		int slstart = 0;
		int slend = 239;

		int slstart_pal = 0;
		int slend_pal = 287;

		unsigned display_fb_xstart = 0;
		unsigned display_fb_ystart = 0;
		int current_readout = 0;
		unsigned next_readout = 0;
		bool last_output_readout = false;
		enum ReadoutType
		{
			READOUT,
			READOUT_SSAA,
		} readout_type;

		TextureMode texture_mode = TextureMode::None;
		SemiTransparentMode semi_transparent = SemiTransparentMode::None;
		ScanoutMode scanout_mode = ScanoutMode::ABGR1555_555;
		ScanoutFilter scanout_filter = ScanoutFilter::None;
		ScanoutFilter scanout_mdec_filter = ScanoutFilter::None;
		bool dither_native_resolution = false;
		bool force_mask_bit = false;
		bool texture_color_modulate = false;
		bool mask_test = false;
		bool display_on = false;
		//bool dither = false;
		bool adaptive_smoothing = true;
		PrimitiveType primitive_type = PrimitiveType::Polygon;

		UVRect UVLimits;
	};

	struct SaveState
	{
		std::vector<uint32_t> vram;
		RenderState state;
		TextureTrackerSaveState tracker_state;
	};

	Renderer(Vulkan::Device &device, unsigned scaling, unsigned msaa, const SaveState *save_state);
	~Renderer();

	void set_track_textures(bool enable);
	void set_dump_textures(bool enable);
	void set_replace_textures(bool enable);

	void set_adaptive_smoothing(bool enable)
	{
		render_state.adaptive_smoothing = enable;
	}

	void set_draw_rect(const Rect &rect);
	inline void set_draw_offset(int x, int y)
	{
		render_state.draw_offset_x = x;
		render_state.draw_offset_y = y;
	}

	inline void set_scissored_invariant(bool invariant) override
	{
		queue.scissor_invariant = invariant;
	}

	void set_texture_window(const TextureWindow &rect);
	inline void set_texture_offset(unsigned x, unsigned y)
	{
		atlas.set_texture_offset(x, y);
		render_state.texture_offset_x = x;
		render_state.texture_offset_y = y;
	}

	inline void set_palette_offset(unsigned x, unsigned y)
	{
		atlas.set_palette_offset(x, y);
		render_state.palette_offset_x = x;
		render_state.palette_offset_y = y;
	}

	Vulkan::BufferHandle copy_cpu_to_vram(const Rect &rect);
	void copy_vram_to_cpu_synchronous(const Rect &rect, uint16_t *vram);
	uint16_t *begin_copy(Vulkan::BufferHandle handle);
	void end_copy(Vulkan::BufferHandle handle);

	void notify_texture_upload(Rect rect, uint16_t *vram);

	void blit_vram(const Rect &dst, const Rect &src);

	void set_vram_framebuffer_coords(unsigned xstart, unsigned ystart)
	{
		last_scanout.reset();
		render_state.valid_fb_rect = false;

		render_state.display_fb_xstart = xstart;
		render_state.display_fb_ystart = ystart;
	}

	void set_horizontal_display_range(int x1, int x2)
	{
		render_state.valid_fb_rect = false;
		render_state.valid_output_rect = false;

		render_state.horiz_start = x1;
		render_state.horiz_end = x2;
	}

	void set_vertical_display_range(int y1, int y2)
	{
		render_state.valid_fb_rect = false;
		render_state.valid_output_rect = false;

		render_state.vert_start = y1;
		render_state.vert_end = y2;
	}

	void set_display_mode(ScanoutMode mode, bool is_pal, bool is_480i, WidthMode width_mode)
	{
		//if (rect != render_state.display_mode || render_state.scanout_mode != mode)
		//	last_scanout.reset();
		last_scanout.reset();
		render_state.valid_fb_rect = false;
		render_state.valid_output_rect = false;

		//render_state.display_mode = rect;
		render_state.scanout_mode = mode;

		render_state.is_pal = is_pal;
		render_state.is_480i = is_480i;
		render_state.width_mode = width_mode;
	}

	void set_current_readout(int yoffset)
	{
		render_state.current_readout = yoffset;
	}

	void set_horizontal_overscan_cropping(bool crop_overscan)
	{
		render_state.crop_overscan = crop_overscan;
		render_state.valid_output_rect = false;
	}

	void set_horizontal_offset_cycles(int offset_cycles)
	{
		render_state.offset_cycles = offset_cycles;
		render_state.valid_output_rect = false;
	}

	void set_visible_scanlines(int slstart, int slend, int slstart_pal, int slend_pal)
	{
		// May need bounds checking to reject bad inputs. Currently assume all inputs are valid.
		render_state.valid_output_rect = false;
		render_state.slstart = slstart;
		render_state.slend = slend;
		render_state.slstart_pal = slstart_pal;
		render_state.slend_pal = slend_pal;
	}

	void set_display_filter(ScanoutFilter filter)
	{
		render_state.scanout_filter = filter;
	}

	void set_mdec_filter(ScanoutFilter mdec_filter)
	{
		render_state.scanout_mdec_filter = mdec_filter;
	}

	void toggle_display(bool enable)
	{
		if (enable != render_state.display_on)
			last_scanout.reset();

		render_state.display_on = enable;
	}

#if 0
	void set_dither(bool dither)
	{
		render_state.dither = dither;
	}
#endif

	void set_dither_native_resolution(bool enable)
	{
		render_state.dither_native_resolution = enable;
	}

	void set_scanout_semaphore(Vulkan::Semaphore semaphore);
	void scanout();
	Vulkan::BufferHandle scanout_to_buffer(bool draw_area, unsigned &width, unsigned &height);
	Vulkan::BufferHandle scanout_vram_to_buffer(unsigned &width, unsigned &height);
	Vulkan::ImageHandle scanout_vram_to_texture(bool scaled = true);
	Vulkan::ImageHandle scanout_to_texture();

	inline void set_texture_mode(TextureMode mode)
	{
		render_state.texture_mode = mode;
		atlas.set_texture_mode(mode);
	}

	inline void set_semi_transparent(SemiTransparentMode state)
	{
		render_state.semi_transparent = state;
	}

	inline void set_force_mask_bit(bool enable)
	{
		render_state.force_mask_bit = enable;
	}

	inline void set_mask_test(bool enable)
	{
		render_state.mask_test = enable;
	}

	inline void set_texture_color_modulate(bool enable)
	{
		render_state.texture_color_modulate = enable;
	}

	inline void set_UV_limits(uint16_t min_u, uint16_t min_v, uint16_t max_u, uint16_t max_v)
	{
		render_state.UVLimits.min_u = min_u;
		render_state.UVLimits.min_v = min_v;
		render_state.UVLimits.max_u = max_u;
		render_state.UVLimits.max_v = max_v;
	}

	inline void set_primitive_type(PrimitiveType type = PrimitiveType::Polygon)
	{
		render_state.primitive_type = type;
	}

	// Draw commands
	void clear_rect(const Rect &rect, FBColor color);
	void draw_line(const Vertex *vertices);
	void draw_triangle(const Vertex *vertices);
	void draw_quad(const Vertex *vertices);

	SaveState save_vram_state();

	void reset_counters()
	{
		counters = {};
	}

	void flush()
	{
		if (cmd)
			device.submit(cmd);
		cmd.reset();
		device.flush_frame();
	}

	Vulkan::Fence flush_and_signal()
	{
		Vulkan::Fence fence;
		if (cmd)
			device.submit(cmd, &fence);
		cmd.reset();
		device.flush_frame();
		return fence;
	}

	struct
	{
		unsigned render_passes = 0;
		unsigned fragment_readback_pixels = 0;
		unsigned fragment_writeout_pixels = 0;
		unsigned draw_calls = 0;
		unsigned vertices = 0;
		unsigned native_draw_calls = 0;
	} counters;

	enum class FilterMode
	{
		NearestNeighbor = 0,
		XBR = 1,
		SABR = 2,
		Bilinear = 3,
		Bilinear3Point = 4,
		JINC2 = 5
	};

	void set_filter_mode(FilterMode mode);
	ScanoutMode get_scanout_mode() const
	{
		return render_state.scanout_mode;
	}

private:
	Vulkan::Device &device;
	unsigned scaling;
	unsigned msaa;
	FilterMode primitive_filter_mode = FilterMode::NearestNeighbor;
	Vulkan::ImageHandle scaled_framebuffer;
	Vulkan::ImageHandle scaled_framebuffer_msaa;
	Vulkan::ImageHandle bias_framebuffer;
	Vulkan::ImageHandle framebuffer;
	Vulkan::Semaphore scanout_semaphore;
	std::vector<Vulkan::ImageViewHandle> scaled_views;
	Vulkan::ImageHandle readout_framebuffer;
	Vulkan::ImageHandle readout_bias;
	std::vector<Vulkan::ImageViewHandle> readout_views;
	FBAtlas atlas;
	bool texture_tracking_enabled = false;
	TextureTracker tracker;

	Vulkan::CommandBufferHandle cmd;

	// HazardListener
	void hazard(StatusFlags flags) override;
	void resolve(Domain target_domain, unsigned x, unsigned y) override;
	void flush_render_pass(const Rect &rect) override;
	void discard_render_pass() override;
	void clear_quad(const Rect &rect, FBColor color, bool candidate) override;

	// TextureUploader
	Vulkan::ImageHandle upload_texture(std::vector<LoadedImage> &image) override;
	Vulkan::ImageHandle create_texture(int width, int height, int levels) override;
	Vulkan::CommandBufferHandle &command_buffer_hack_fixme() override;

	void hd_texture_uniforms(HdTextureHandle hd_texture_index);
	void update_hd_texture(const Rect &imageRect, const Rect &dstRect, const void *pixels);
	void update_hd_textures();
	HdTextureHandle get_hd_texture_index(const Rect &uvlimits, bool &fastpath_capable_out, bool &cache_hit_out);

	struct
	{
		Vulkan::Program *copy_to_vram;
		Vulkan::Program *copy_to_vram_masked;
		Vulkan::Program *unscaled_quad_blitter;
		Vulkan::Program *scaled_quad_blitter;
		Vulkan::Program *unscaled_dither_quad_blitter;
		Vulkan::Program *scaled_dither_quad_blitter;
		Vulkan::Program *bpp24_quad_blitter;
		Vulkan::Program *bpp24_yuv_quad_blitter;
		Vulkan::Program *resolve_to_scaled;
		Vulkan::Program *resolve_to_unscaled;

		Vulkan::Program *blit_vram_scaled;
		Vulkan::Program *blit_vram_scaled_masked;

		Vulkan::Program *blit_vram_cached_scaled;
		Vulkan::Program *blit_vram_cached_scaled_masked;
		Vulkan::Program *blit_vram_msaa_cached_scaled;
		Vulkan::Program *blit_vram_msaa_cached_scaled_masked;

		Vulkan::Program *blit_vram_unscaled;
		Vulkan::Program *blit_vram_unscaled_masked;
		Vulkan::Program *blit_vram_cached_unscaled;
		Vulkan::Program *blit_vram_cached_unscaled_masked;

		Vulkan::Program *opaque_flat;
		Vulkan::Program *opaque_textured;
		Vulkan::Program *opaque_spr_textured;
		Vulkan::Program *opaque_semi_transparent;
		Vulkan::Program *semi_transparent;
		Vulkan::Program *semi_transparent_masked_add;
		Vulkan::Program *semi_transparent_masked_average;
		Vulkan::Program *semi_transparent_masked_sub;
		Vulkan::Program *semi_transparent_masked_add_quarter;
		Vulkan::Program *flat_masked_add;
		Vulkan::Program *flat_masked_average;
		Vulkan::Program *flat_masked_sub;
		Vulkan::Program *flat_masked_add_quarter;

		Vulkan::Program *mipmap_resolve;
		Vulkan::Program *mipmap_dither_resolve;
		Vulkan::Program *mipmap_energy_first;
		Vulkan::Program *mipmap_energy;
		Vulkan::Program *mipmap_energy_blur;
	} pipelines;

	Vulkan::ImageHandle dither_lut;

	void init_pipelines();
	void init_primitive_pipelines();
	void init_primitive_feedback_pipelines();
	void ensure_command_buffer();

	RenderState render_state;

	struct BufferVertex
	{
		float x, y, z, w;
		uint32_t color;
		TextureWindow window;
		int16_t pal_x, pal_y, params;
		int16_t u, v, base_uv_x, base_uv_y;
		uint16_t min_u, min_v, max_u, max_v;
	};

	struct BlitInfo
	{
		uint32_t src_offset[2];
		uint32_t dst_offset[2];
		uint32_t extent[2];
		uint32_t mask;
		uint32_t sample;
	};

	struct SemiTransparentState
	{
		int scissor_index;
		HdTextureHandle hd_texture_index;
		SemiTransparentMode semi_transparent;
		bool textured;
		bool masked;
		bool w1; // w of vertice = 1

		bool operator==(const SemiTransparentState &other) const
		{
			return scissor_index == other.scissor_index && hd_texture_index == other.hd_texture_index &&
			       semi_transparent == other.semi_transparent && textured == other.textured && masked == other.masked;
		}

		bool operator!=(const SemiTransparentState &other) const
		{
			return !(*this == other);
		}
	};

	struct ClearCandidate
	{
		Rect rect;
		FBColor color;
		float z;
	};

	struct PrimitiveInfo {
		unsigned triangle_index;
		int scissor_index;
		HdTextureHandle hd_texture_index;

		// needed for emplace_back
		PrimitiveInfo(unsigned triangle_index, int scissor_index, HdTextureHandle hd_texture_index)
			: triangle_index(triangle_index), scissor_index(scissor_index), hd_texture_index(hd_texture_index)
		{

		}
	};

	struct OpaqueQueue
	{
		// Non-textured primitives.
		std::vector<BufferVertex> opaque;
		std::vector<PrimitiveInfo> opaque_scissor;

		// Sprite primitives are stored separately when Texture Filtering
		// is disabled for sprites so a separate draw call may be issued.
		// Polygon primitives with w = 1 are used for detected 2D elements
		// drawn as polygons for the sake of Widescreen Aspect hack.
		// We assume a polygon is intended for 2D when its w = 1.
		// For SemiTrans the w1 is stored in State.

		// Textured primitives, no semi-transparency.
		std::vector<BufferVertex> opaque_textured;
		std::vector<PrimitiveInfo> opaque_textured_scissor;
		std::vector<BufferVertex> opaque_spr_textured; // Sprite primitives
		std::vector<PrimitiveInfo> opaque_spr_textured_scissor;

		// Textured primitives, semi-transparency enabled.
		std::vector<BufferVertex> semi_transparent_opaque;
		std::vector<PrimitiveInfo> semi_transparent_opaque_scissor;

		std::vector<BufferVertex> semi_transparent;
		std::vector<SemiTransparentState> semi_transparent_state;

		// Polygon primitives, w = 1
		std::vector<BufferVertex> opaque_poly_w1;
		std::vector<PrimitiveInfo> opaque_poly_w1_scissor;
		std::vector<BufferVertex> opaque_poly_w1_textured;
		std::vector<PrimitiveInfo> opaque_poly_w1_textured_scissor;
		std::vector<BufferVertex> semi_trans_opaque_poly_w1;
		std::vector<PrimitiveInfo> semi_trans_opaque_poly_w1_scissor;

		std::vector<Vulkan::ImageHandle> textures;

		std::vector<VkRect2D> scaled_resolves;
		std::vector<VkRect2D> unscaled_resolves;
		std::vector<BlitInfo> scaled_blits;
		std::vector<BlitInfo> scaled_masked_blits;
		std::vector<BlitInfo> unscaled_blits;
		std::vector<BlitInfo> unscaled_masked_blits;

		std::vector<VkRect2D> scissors;
		std::vector<ClearCandidate> clear_candidates;
		VkRect2D default_scissor;
		bool scissor_invariant = false;
	} queue;
	unsigned primitive_index = 0;
	bool render_pass_is_feedback = false;
	float last_uv_scale_x, last_uv_scale_y;

	void dispatch(const std::vector<BufferVertex> &vertices, std::vector<PrimitiveInfo> &scissors);
	void render_opaque_primitives(PrimitiveType primitive_type);
	void render_opaque_texture_primitives(PrimitiveType primitive_type);
	void render_semi_transparent_opaque_texture_primitives(PrimitiveType primitive_type);
	void render_semi_transparent_primitives();
	void reset_queue();

	float allocate_depth(const Rect &rect);

	void build_attribs(BufferVertex *verts, const Vertex *vertices, unsigned count, HdTextureHandle &hd_texture_index);
	void build_line_quad(Vertex *quad, const Vertex *line);
	std::vector<BufferVertex> *select_pipeline(unsigned prims, int scissor, HdTextureHandle hd_texture);

	void flush_resolves();
	void flush_blits();
	void reset_scissor_queue();
	const ClearCandidate *find_clear_candidate(const Rect &rect) const;

	Rect compute_window_rect(const TextureWindow &window);

	Vulkan::ImageHandle last_scanout;
	Vulkan::ImageHandle reuseable_scanout;
	const DisplayRect &compute_display_rect();

	const Rect &compute_vram_framebuffer_rect();

	void mipmap_framebuffer(bool readout);
	void mipmap_readout();
	void scanout_to_readout(unsigned next_readout);
	void scanout_to_readout(Rect next_draw);
	Vulkan::BufferHandle quad;
};
}
