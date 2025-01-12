#ifdef DEBUG_MAIN
#define DEBUG_MODULE main
#endif
#include "debug.h"

#include <stdint.h>
#include "framebuffer.h"
#include "uart.h"
#include "board.h"
#include "cpu.h"
#include "render.h"
#include "sim.h"
#include "fixed-point.h"
#include "time.h"
#include "font.h"
#include "key.h"

__xdata uint8_t cube[8][8][8];

#define CLOCK_DISPLAY_FIELD_LO min_bcd
#define CLOCK_DISPLAY_FIELD_HI hour_bcd

static __xdata union {
	struct {
		mat4x4_t m;
		tex2D_t texture;
	} bench_render_tex2d;
	struct {
		mat4x4_t m;
		tex2D_t texture;
	} render_frame;
	struct {
		mat4x4_t m;
		vec3_t v;
		tex2D_t texture;
	} mode_clock_render;
	struct {
		mat4x4_t m;
		vec3_t v;
		tex2D_t texture;
	} mode_settime_render;
} _xso;

typedef enum {
	MODE_CLOCK,
	MODE_SETTIME,
} mode_t;

typedef struct {
	mode_t mode;
	time_t time;
	union {
		struct {
			uint16_t start_tick;
			uint8_t step;
			uint16_t old_digits;
			uint16_t new_digits;
			uint8_t pending;
			uint8_t dark;
		} mode_clock;
		struct {
			uint16_t digits;
			uint8_t current_digit;
		} mode_settime;
	};
} display_state_t;

static __xdata display_state_t _display_state;

static void _next_mode();

#define XSO(X) _xso.OVERLAY_FIELD.X
#define xso_m XSO(m)
#define xso_texture XSO(texture)

void pca_init() __critical
{
	uint8_t tmp;

	tmp = AUXR1;
	tmp &= ~PCA_P4_val;
	AUXR1 = tmp;

	CL = 0;
	CH = 0;
	CMOD = CPS_div12;
	CCON = CR_val;
}

void pca_isr() __interrupt(7)
{
	if (FB_CCF) {
		FB_CCF = 0;
		fb_isr();
	}
	if (TIME_CCF) {
		TIME_CCF = 0;
		time_isr();
		key_isr();
	}
}

#define OVERLAY_FIELD render_frame
void render_frame(__xdata fb_frame_t *fb)
{
	static __xdata fp_t a = FP_0;
	memcpy(&XSO(texture),
	       font_get_texture('4'),
	       sizeof(tex2D_t));

	fp_identity_mat4x4(&XSO(m));
	fp_pre_rotate_mat4x4_z(&XSO(m), a);

	render_clear(fb);
	render_tex2D(fb, &XSO(texture), &XSO(m));

	a = fp_add(a, FP_sPI/16);
}
#undef OVERLAY_FIELD

#define OVERLAY_FIELD render_frame
void bench_render_tex2d(__xdata fb_frame_t *fb)
{
	int8_t i;

	memset(&XSO(texture), 0xff, sizeof(tex2D_t));
	fp_identity_mat4x4(&XSO(m));

	for (i = 0; i < 100; i++) {
		render_tex2D(fb, &XSO(texture), &XSO(m));
		note("%02x", i);
	}
	sim_stop();
}
#undef OVERLAY_FIELD

static const mat4x4_t mat_rotate = {
	{  FP_0,  FP_1,	 FP_0,	FP_0, },
	{  FP_0,  FP_0, -FP_1,	FP_0, },
	{ -FP_1,  FP_0,	 FP_0,	FP_0, },
	{  FP_0,  FP_0,	 FP_0,	FP_1, },
};

static const vec4_t pos[4] = {
	{ fp_add(FP_3, FP_1/2), FP_1, FP_0 },
	{ fp_add(FP_1, FP_1/2), -FP_1, FP_0 },
	{ fp_add(-FP_1, -FP_1/2), FP_1, FP_0 },
	{ fp_add(-FP_3, -FP_1/2), -FP_1, FP_0 },
};

static void _render_cube(__xdata fb_frame_t *fb)
{
	uint8_t row;
	uint8_t col;
	uint8_t z;

	for (row = 0; row < 8; row++) {
		for (col = 0; col < 8; col++) {
			for (z = 0; z < 8; z++) {
				if (cube[row][col][z]) {
					render_set_pixel(fb, row, col, z);
				}
			}
		}
	}
}

#define OVERLAY_FIELD mode_clock_render
static void _mode_clock_render(__xdata fb_frame_t *fb) __reentrant
{
	int i;

	uint16_t new_digits = _display_state.mode_clock.new_digits;
	uint16_t old_digits = _display_state.mode_clock.old_digits;

	for (i = 0; i < 4; i++) {
		uint8_t new_digit = new_digits & 0xf;
		uint8_t old_digit = old_digits & 0xf;

		memcpy(&XSO(v), &pos[i], sizeof(vec3_t));
		if ((new_digit == old_digit) || (_display_state.mode_clock.step > 8)) {
			memcpy(&XSO(texture),
			       font_get_texture('0' + new_digit),
			       sizeof(tex2D_t));
		} else {
			if (XSO(v)[0] > 0) {
				XSO(v)[0] = fp_add(XSO(v)[0], FP_FROM_INT(_display_state.mode_clock.step));
				if (XSO(v)[0] > FP_4) {
					XSO(v)[0] = fp_sub(XSO(v)[0], FP_FROM_INT(8));
					memcpy(&XSO(texture),
					       font_get_texture('0' + new_digit),
					       sizeof(tex2D_t));
				} else {
					memcpy(&XSO(texture),
					       font_get_texture('0' + old_digit),
					       sizeof(tex2D_t));
				}
			} else {
				XSO(v)[0] = fp_sub(XSO(v)[0], FP_FROM_INT(_display_state.mode_clock.step));
				if (XSO(v)[0] < -FP_4) {
					XSO(v)[0] = fp_add(XSO(v)[0], FP_FROM_INT(8));
					memcpy(&XSO(texture),
					       font_get_texture('0' + new_digit),
					       sizeof(tex2D_t));
				} else {
					memcpy(&XSO(texture),
					       font_get_texture('0' + old_digit),
					       sizeof(tex2D_t));
				}
			}
		}

		memcpy(&XSO(m), &mat_rotate, sizeof(mat4x4_t));

		fp_post_translate_mat4x4_vec3(&XSO(m), &XSO(v));

		render_tex2D(fb, &XSO(texture), &XSO(m));

		new_digits >>= 4;
		old_digits >>= 4;
	}
	_display_state.mode_clock.pending = 0;
}
#undef OVERLAY_FIELD

static uint16_t _mode_clock_digits()
{
	uint16_t tmp;
	tmp = ((uint16_t)_display_state.time.CLOCK_DISPLAY_FIELD_HI) << 8;
	tmp |= _display_state.time.CLOCK_DISPLAY_FIELD_LO;
	return tmp;
}

static void _mode_clock_setup()
{
	uint16_t tmp = _mode_clock_digits();
	_display_state.mode = MODE_CLOCK;
	_display_state.mode_clock.old_digits = tmp;
	_display_state.mode_clock.new_digits = tmp;
	_display_state.mode_clock.step = 9;
	_display_state.mode_clock.start_tick = 0;
	_display_state.mode_clock.pending = 1;
	_display_state.mode_clock.dark = 0;
}

static void _mode_clock_handle()
{
	uint16_t tmp;

	switch (key_consume_event()) {
	case KEY_MAKE_EVENT(KEY_NEXT, KEY_LONG):
		_next_mode();
		return;
	case KEY_MAKE_EVENT(KEY_START, KEY_PRESS):
		_display_state.mode_clock.dark ^= 1;
		_display_state.mode_clock.pending = 1;
		break;
	default:
		break;
	}

	/* Check if a back framebuffer is available */
	if (fb_back_frame_complete()) {
		/* early exit if not */
		return;
	}

	/* update animation */
	if (_display_state.mode_clock.step > 8) {
		tmp = _mode_clock_digits();

		if (_display_state.mode_clock.new_digits != tmp) {
			_display_state.mode_clock.old_digits =
				_display_state.mode_clock.new_digits;
			_display_state.mode_clock.new_digits = tmp;
			_display_state.mode_clock.start_tick = _display_state.time.ticks;
			_display_state.mode_clock.step = 0;
			_display_state.mode_clock.pending = 1;
		}
	} else {
		tmp = _display_state.time.ticks - _display_state.mode_clock.start_tick;
		uint8_t tmp2 = _display_state.mode_clock.step;
		if (tmp >= 36) {
			_display_state.mode_clock.step = 9;
		} else {
			_display_state.mode_clock.step = ((uint8_t)tmp) / 4;
		}
		if (tmp2 != _display_state.mode_clock.step) {
			_display_state.mode_clock.pending = 1;
		}
	}

	/* render frame */
	if (_display_state.mode_clock.pending != 0) {
		__xdata fb_frame_t *fb = fb_back_frame();
		render_clear(fb);
		if (!_display_state.mode_clock.dark)
			_mode_clock_render(fb);
		fb_back_frame_completed();
		note("f\n");
	}
}


static void render_cube()
{
	switch (key_consume_event()) {
	case KEY_MAKE_EVENT(KEY_NEXT, KEY_LONG):
		_next_mode();
		return;
	case KEY_MAKE_EVENT(KEY_START, KEY_PRESS):
		_display_state.mode_clock.dark ^= 1;
		_display_state.mode_clock.pending = 1;
		break;
	default:
		break;
	}

	/* Check if a back framebuffer is available */
	if (fb_back_frame_complete()) {
		/* early exit if not */
		return;
	}

	/* render frame */
	__xdata fb_frame_t *fb = fb_back_frame();
	render_clear(fb);
	if (!_display_state.mode_clock.dark)
		_render_cube(fb);
	fb_back_frame_completed();
	note("f\n");
}

#define OVERLAY_FIELD mode_settime_render
static void _mode_settime_render(__xdata fb_frame_t *fb) __reentrant
{
	int i;

	uint16_t digits = _display_state.mode_settime.digits;

	for (i = 0; i < 4; i++, digits >>= 4) {
		if ((i == _display_state.mode_settime.current_digit) &&
		    (_display_state.time.tick < TIME_TICK_HZ / 2)) {
			continue;
		}

		uint8_t digit = digits & 0xf;

		memcpy(&XSO(texture),
		       font_get_texture('0' + digit),
		       sizeof(tex2D_t));

		memcpy(&XSO(m), &mat_rotate, sizeof(mat4x4_t));
		fp_post_translate_mat4x4_vec3(&XSO(m), &pos[i]);

		render_tex2D(fb, &XSO(texture), &XSO(m));


	}
}
#undef OVERLAY_FIELD

static void _mode_settime_setup()
{
	_display_state.mode = MODE_SETTIME;
	_display_state.mode_settime.digits = _mode_clock_digits();
	_display_state.mode_settime.current_digit = 0;
}

static void _mode_settime_change_digit(bool increment) __critical
{
	uint8_t tmp;
	time_get(&_display_state.time);

	switch (_display_state.mode_settime.current_digit) {
	case 0:
		tmp = _display_state.time.CLOCK_DISPLAY_FIELD_LO;
		_display_state.time.CLOCK_DISPLAY_FIELD_LO &= 0xf0;
		break;
	case 1:
		tmp = _display_state.time.CLOCK_DISPLAY_FIELD_LO;
		tmp >>= 4;
		_display_state.time.CLOCK_DISPLAY_FIELD_LO &= 0xf;
		break;
	case 2:
		tmp = _display_state.time.CLOCK_DISPLAY_FIELD_HI;
		_display_state.time.CLOCK_DISPLAY_FIELD_HI &= 0xf0;
		break;
	case 3:
		tmp = _display_state.time.CLOCK_DISPLAY_FIELD_HI;
		tmp >>= 4;
		_display_state.time.CLOCK_DISPLAY_FIELD_HI &= 0xf;
		break;
	default:
		return;
	}

	if (increment) {
		if (tmp == 9) {
			tmp = 0;
		} else {
			tmp++;
		}
	} else {
		if (tmp == 0) {
			tmp = 9;
		} else {
			tmp--;
		}
	}

	if (_display_state.mode_settime.current_digit & 1) {
		tmp <<= 4;
	}

	if ((_display_state.mode_settime.current_digit & 2) == 0) {
		_display_state.time.CLOCK_DISPLAY_FIELD_LO |= tmp;
	} else {
		_display_state.time.CLOCK_DISPLAY_FIELD_HI |= tmp;
	}

	time_set(&_display_state.time);
}

static void _mode_settime_handle()
{
	switch (key_consume_event()) {
	case KEY_MAKE_EVENT(KEY_NEXT, KEY_LONG):
		_next_mode();
		return;
	case KEY_MAKE_EVENT(KEY_START, KEY_PRESS):
		_mode_settime_change_digit(false);
		break;
	case KEY_MAKE_EVENT(KEY_CYCLE, KEY_PRESS):
		_mode_settime_change_digit(true);
		break;
	case KEY_MAKE_EVENT(KEY_NEXT, KEY_PRESS):
		_display_state.mode_settime.current_digit++;
		_display_state.mode_settime.current_digit %= 4;
		break;
	default:
		break;
	}

	_display_state.mode_settime.digits = _mode_clock_digits();

	/* Check if a back framebuffer is available */
	if (fb_back_frame_complete()) {
		/* early exit if not */
		return;
	}

	/* render frame */
	__xdata fb_frame_t *fb = fb_back_frame();
	render_clear(fb);
	_mode_settime_render(fb);
	fb_back_frame_completed();
}


static void _next_mode()
{
	switch (_display_state.mode) {
	default:
	case MODE_CLOCK:
		_mode_settime_setup();
		break;
	case MODE_SETTIME:
		_mode_clock_setup();
		break;
	}
}

void main(void)
{
	EA = 1;
	uart_init();
	printf("iCube started [%s]\n",
	       sim_detect() ? "SIM" : "LIVE");

	fb_init();
	printf("Framebuffer initialized\n");

	time_init();
	printf("Time initialized\n");

	key_init();
	printf("Keys initialized\n");

	pca_init();
	printf("PCA initialized\n");

	time_get(&_display_state.time);

	uint8_t i=0;
	_mode_clock_setup();

	for (;;PCON |= 1) {
		time_get(&_display_state.time);

		switch (_display_state.mode) {
		default:
			_mode_clock_setup();
		case MODE_CLOCK:
			render_cube();
			break;
		case MODE_SETTIME:
			_mode_settime_handle();
			break;
		}
	}
}
