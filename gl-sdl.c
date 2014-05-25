/******************************************************************************
    Copyright (C) 2014 by Zachary Lund <admin@computerquip.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "gl-subsystem.h"

#include <X11/Xlib.h>
#include <glad/glad.h>

#include "SDL.h"
#include "SDL_syswm.h"

struct gl_windowinfo {
	SDL_Window *sdl_window; 
};

struct gl_platform {
	SDL_GLContext context;
	struct gs_swap_chain swap;
};

extern struct gs_swap_chain *gl_platform_getswap(struct gl_platform *platform)
{
	return &platform->swap;
}

extern struct gl_windowinfo *gl_windowinfo_create(struct gs_init_data *info)
{
	struct gl_windowinfo *wi = bzalloc(sizeof(struct gl_windowinfo));
	SDL_SysWMinfo wm_info;

	wi->sdl_window = 
		SDL_CreateWindow(
			"OBS Studio", 0, 0, 
			info->cx, info->cy,
		  	SDL_WINDOW_OPENGL | 
		  	SDL_WINDOW_BORDERLESS |
		  	SDL_WINDOW_HIDDEN
		);

	if (!wi->sdl_window) {
		blog(LOG_ERROR, "Failed to create SDL Window!");

	}

	/* There be dragons and New Jersians' hea. Tread carefully. */
	/* Ideally, we would deal with every platform here instead of just X11. */
	SDL_VERSION(&wm_info.version);

	if (!SDL_GetWindowWMInfo(wi->sdl_window, &wm_info)) {
		blog(LOG_ERROR, "Failed to fetch windowing system information!");
		goto fail0;
	}

	switch (wm_info.subsystem) {
	case SDL_SYSWM_X11: {
		int result = 0;

		result = XReparentWindow(
			wm_info.info.x11.display, 
			wm_info.info.x11.window,
			info->window.id,
			0, 0
		);

		if (!result) {
			blog(LOG_ERROR, "Failed to reparent SDL window!");
			goto fail0;
		}
	}
	}

	return wi;

fail0:
	bfree(wi);
	return NULL;
}

extern void gl_windowinfo_destroy(struct gl_windowinfo *wi)
{
	SDL_DestroyWindow(wi->sdl_window);

	bfree(wi);
}

extern void gl_getclientsize(struct gs_swap_chain *swap,
			     uint32_t *width, uint32_t *height)
{
	SDL_GetWindowSize(swap->wi->sdl_window, (int*)width, (int*)height);
}

static void print_info_stuff(struct gs_init_data *info)
{
	blog(LOG_INFO,
		"X and Y: %i %i\n"
		"Backbuffers: %i\n"
		"Color Format: %i\n"
		"ZStencil Format: %i\n"
		"Adapter: %i\n",
		info->cx, info->cy,
		info->num_backbuffers,
		info->format, info->zsformat,
		info->adapter
	);
}

struct gl_platform *gl_platform_create(device_t device,
		struct gs_init_data *info)
{
	struct gl_platform *plat = bzalloc(sizeof(struct gl_platform));

	if (!plat) return NULL;

	print_info_stuff(info);

	device->plat = plat;

	plat->swap.wi                   = gl_windowinfo_create(info);

	if (!plat->swap.wi) goto fail0;

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 			SDL_GL_CONTEXT_DEBUG_FLAG);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 	SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,	3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,	2);

	plat->swap.device = device;
	plat->swap.info = *info;
	plat->context = SDL_GL_CreateContext(plat->swap.wi->sdl_window);

	if (!plat->context){
		blog(LOG_ERROR, "Failed to create context!");
		goto fail1;
	}

	if (SDL_GL_MakeCurrent(plat->swap.wi->sdl_window, plat->context) != 0) {
		blog(LOG_ERROR, "Failed to make context current.");
		goto fail2;
	}

	/* We originally hid it so as to not show the ugliness of a  */
	SDL_ShowWindow(plat->swap.wi->sdl_window);

	if (!gladLoadGL()) {
		blog(LOG_ERROR, "Failed to load OpenGL entry functions.");
		goto fail3;
	}

	blog(LOG_INFO, "OpenGL version: %s\n", glGetString(GL_VERSION));

	/* We assume later that cur_swap is already set. */
	device->cur_swap = &plat->swap;

	blog(LOG_INFO, "Created new platform data");

	return plat;

fail3:
	SDL_GL_MakeCurrent(plat->context, NULL);

fail2:
	SDL_GL_DeleteContext(plat->context);

fail1:
	gl_windowinfo_destroy(plat->swap.wi);

fail0:
	bfree(plat);
	return NULL;
}

void gl_platform_destroy(struct gl_platform *platform)
{
	if (!platform)
		return;

	SDL_Window* window = platform->swap.wi->sdl_window;
	SDL_GLContext context = platform->context;

	SDL_GL_MakeCurrent(window, NULL);
	SDL_DestroyWindow(window);
	SDL_GL_DeleteContext(context);
	gl_windowinfo_destroy(platform->swap.wi);
	bfree(platform);
}

void device_entercontext(device_t device)
{
	SDL_GLContext context = device->plat->context;
	SDL_Window *window = device->cur_swap->wi->sdl_window;

	if (SDL_GL_MakeCurrent(window, context) != 0) {
		blog(LOG_ERROR, "Failed to make context current.");
	}
}

void device_leavecontext(device_t device)
{
	SDL_Window* window = device->cur_swap->wi->sdl_window;

	if (SDL_GL_MakeCurrent(window, NULL) != 0) {
		blog(LOG_ERROR, "Failed to reset current context.");
	}
}

void gl_update(device_t device)
{
	SDL_Window* window = device->cur_swap->wi->sdl_window;

	blog(LOG_INFO, "Test");

	SDL_SetWindowSize(
		window,
		device->cur_swap->info.cx, 
		device->cur_swap->info.cy
	);
}

void device_load_swapchain(device_t device, swapchain_t swap)
{
	if (!swap)
		swap = &device->plat->swap;

	if (device->cur_swap == swap)
		return;

	SDL_Window* window = swap->wi->sdl_window;
	SDL_GLContext ctx = device->plat->context;

	device->cur_swap = swap;

	if (SDL_GL_MakeCurrent(window, ctx) != 0) {
		blog(LOG_ERROR, "Failed to make context current.");
	}
}

void device_present(device_t device)
{
	SDL_Window* window = device->cur_swap->wi->sdl_window;

	SDL_GL_SwapWindow(window);
}
