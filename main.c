#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <ctype.h>

#include <unistd.h>
#include <sys/types.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>     
#include <netdb.h>      

#include <pthread.h>      

#include <SDL.h>
//#include <SDL_opengl.h>
#include "glad/glad.h"

#include "args.h"

typedef uint64_t uint64;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t uint8;

#define nil NULL


#define WIDTH 1024
#define HEIGHT 1024
#define BORDER 2
#define BWIDTH (WIDTH+2*BORDER)
#define BHEIGHT (HEIGHT+2*BORDER)

char *argv0;

SDL_Window *window;
int netfd;
uint32 userevent;

GLuint vbo;
GLuint vao;
GLint program;
GLint point_program, excite_program, combine_program;
GLuint gltex;
GLuint whiteTex, yellowTex[2];
GLuint whiteFBO, yellowFBO[2];
int flip;

void
panic(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "error: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
	SDL_Quit();
}

int
readn(int fd, void *data, int n)
{       
	int m;

	while(n > 0){
		m = read(fd, data, n);
		if(m <= 0)
			return -1;
		data += m;
		n -= m;
	}
	return 0;
}

int
dial(const char *host, int port)
{
	char portstr[32];
	int sockfd;
	struct addrinfo *result, *rp, hints;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	snprintf(portstr, 32, "%d", port);
	if(getaddrinfo(host, portstr, &hints, &result)){
		perror("error: getaddrinfo");
		return -1;
	}

	for(rp = result; rp; rp = rp->ai_next){
		sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if(sockfd < 0)
			continue;
		if(connect(sockfd, rp->ai_addr, rp->ai_addrlen) >= 0)
			goto win;
		close(sockfd);
	}
	freeaddrinfo(result);
	perror("error");
	return -1;

win:
	freeaddrinfo(result);
	return sockfd;
}

int
serve1(int port)
{
	int sockfd, confd;
	socklen_t len;
	struct sockaddr_in server, client;
	int x;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0){
		perror("error: socket");
		return -1;
	}

	x = 1;
	setsockopt (sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&x, sizeof x);

	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(port);
	if(bind(sockfd, (struct sockaddr*)&server, sizeof(server)) < 0){
		perror("error: bind");
		return -1;
	}
	listen(sockfd, 5);
	len = sizeof(client);
	while(confd = accept(sockfd, (struct sockaddr*)&client, &len),
	      confd >= 0)
		return confd;
	perror("error: accept");
	return -1;
}

void    
printlog(GLuint object)
{
	GLint log_length;
	char *log;

	if(glIsShader(object))
		glGetShaderiv(object, GL_INFO_LOG_LENGTH, &log_length);
	else if(glIsProgram(object))
		glGetProgramiv(object, GL_INFO_LOG_LENGTH, &log_length);
	else{
		fprintf(stderr, "printlog: Not a shader or a program\n");
		return;
	}

	log = (char*) malloc(log_length);
	if(glIsShader(object))
		glGetShaderInfoLog(object, log_length, NULL, log);
	else if(glIsProgram(object))
		glGetProgramInfoLog(object, log_length, NULL, log);
	fprintf(stderr, "%s", log);
	free(log);
}

GLint
compileshader(GLenum type, const char *src)
{
	GLint shader, success;

	shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if(!success){
		fprintf(stderr, "Error in shader\n");
printf("%s\n", src);
		printlog(shader);
exit(1);
		return -1;
	}
	return shader;
}

typedef struct Point Point;
struct Point
{
	int x, y;
	int i;
	int time;
};
int indices[1024*1024];
Point newpoints[1024*1024];
int nnewpoints;
Point points[1024*1024];
int npoints;

GLint
linkprogram(GLint vs, GLint fs)
{
	GLint program, success;

	program = glCreateProgram();

	glBindAttribLocation(program, 0, "in_pos");
	glBindAttribLocation(program, 1, "in_uv");

	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if(!success){
		fprintf(stderr, "glLinkProgram:");
		printlog(program);
exit(1);
		return -1;
	}

	glUseProgram(program);
	glUniform1i(glGetUniformLocation(program, "tex0"), 0);
	glUniform1i(glGetUniformLocation(program, "tex1"), 1);

	return program;
}

float sizefoo = 0.005f;
float intfoo = 1.0f;
int scalefoo = 0;
int xxfoo = 8;

float maxsz = 0.0055f;
float minsz = 0.0018f;
float maxbr = 1.00f;
float minbr = 0.25f;

void
draw(void)
{
	int w, h;
	glViewport(0, 0, BWIDTH, BHEIGHT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	/* draw white phosphor */
	glEnable(GL_BLEND);
//	glBlendEquation(GL_MAX);
	glBlendFunc(GL_ONE, GL_ONE);

	glBindFramebuffer(GL_FRAMEBUFFER, whiteFBO);
	glClear(GL_COLOR_BUFFER_BIT);
	glUseProgram(point_program);
        int coord = glGetUniformLocation(point_program, "coord");
        int age = glGetUniformLocation(point_program, "age");
        int intensity = glGetUniformLocation(point_program, "intensity");

	for(int i = 0; i < npoints; i++) {
		float x = (float)(points[i].x+BORDER)/BWIDTH;
		float y = (float)(points[i].y+BORDER)/BHEIGHT;
// teco uses 3
// spacewar uses 4
// DDT uses 7
		float sz = minsz + (maxsz-minsz)*(points[i].i/7.0f);
		float br = minbr + (maxbr-minbr)*(points[i].i/7.0f);

//		glUniform3f(coord, x*2-1.0f, y*2-1.0f, 0.005f);
//		glUniform3f(coord, x*2-1.0f, y*2-1.0f, 0.003f);
//		glUniform3f(coord, x*2-1.0f, y*2-1.0f, sz);

		glUniform3f(coord, x*2-1.0f, y*2-1.0f, sz);
		glUniform1f(intensity, br);

//		glUniform3f(coord, x*2-1.0f, y*2-1.0f, sizefoo);
//		glUniform1f(intensity, intfoo);

		glUniform1f(age, points[i].time/50000.0f);
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDisable(GL_BLEND);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, whiteTex);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, yellowTex[flip]);


	/* draw and age yellow layer */
	glBindFramebuffer(GL_FRAMEBUFFER, yellowFBO[!flip]);
	glClear(GL_COLOR_BUFFER_BIT);
	glUseProgram(excite_program);
	glDrawArrays(GL_TRIANGLES, 0, 6);


	/* compose final image */
	SDL_GetWindowSize(window, &w, &h);
	if(w > h)
		glViewport((w-h)/2, 0, h, h);
	else
		glViewport(0, (h-w)/2, w, w);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, yellowTex[!flip]);
	glClear(GL_COLOR_BUFFER_BIT);
	glUseProgram(combine_program);
	glDrawArrays(GL_TRIANGLES, 0, 6);


	/* clear state */
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);

	flip = !flip;
	SDL_GL_SwapWindow(window);
}

#ifdef GLES
#define glslheader "#version 100\nprecision highp float; precision highp int;\n" \
	"#define VSIN attribute\n" \
	"#define VSOUT varying\n" \
	"#define FSIN varying\n"
#define outcolor
#define output "gl_FragColor = color;\n"
#else
#define glslheader "#version 130\n" \
	"#define VSIN in\n" \
	"#define VSOUT out\n" \
	"#define FSIN in\n"
#define outcolor
#define output "gl_FragColor = color;\n"
#endif

const char *vs_src =
glslheader
"VSIN vec2 in_pos;\n"
"VSIN vec2 in_uv;\n"
"VSOUT vec2 v_uv;\n"
"void main()\n"
"{\n"
"	v_uv = in_uv;\n"
"	gl_Position = vec4(in_pos.x, in_pos.y, -0.5, 1.0);\n"
"}\n";

const char *fs_src = 
glslheader
outcolor
"FSIN vec2 v_uv;\n"
"uniform sampler2D tex0;\n"
"void main()\n"
"{\n"
"	vec2 uv = vec2(v_uv.x, 1.0-v_uv.y);\n"
"	vec4 color = texture2D(tex0, uv);\n"
output
"}\n";

const char *point_vs_src =
glslheader
"VSIN vec2 in_pos;\n"
"VSIN vec2 in_uv;\n"
"VSOUT vec2 v_uv;\n"
"VSOUT float v_fade;\n"
"uniform vec3 coord;\n"
"uniform float age;\n"
"#define scl coord.z\n"
"void main()\n"
"{\n"
"	v_uv = in_uv;\n"
"	v_fade = pow(0.5, age);\n"
"	gl_Position = vec4(in_pos.x*scl+coord.x, in_pos.y*scl+coord.y, -0.5, 1.0);\n"
"}\n";

const char *point_fs_src = 
glslheader
outcolor
"FSIN vec2 v_uv;\n"
"FSIN float v_fade;\n"
"uniform float intensity;\n"
"void main()\n"
"{\n"
"	float dist = pow(length(v_uv*2.0 - 1.0), 2.0);\n"
"	float intens = clamp(1.0-dist, 0.0, 1.0)*intensity;\n"
"	vec4 color = vec4(0);\n"
"	color.x = intens*v_fade;\n"
"	color.y = intens;\n"
"	color.z = 1.0;\n"
output
"}\n";

const char *excite_fs_src = 
glslheader
outcolor
"FSIN vec2 v_uv;\n"
"uniform sampler2D tex0;\n"
"uniform sampler2D tex1;\n"
"void main()\n"
"{\n"
"	vec2 uv = vec2(v_uv.x, v_uv.y);\n"
"	vec4 white = texture2D(tex0, uv);\n"
"	vec4 yellow = texture2D(tex1, uv);\n"
"	vec4 color = max(vec4(white.y*white.z), 0.987*yellow);\n"
"	color = floor(color*255.0)/255.0;\n"
output
"}\n";

const char *combine_fs_src = 
glslheader
outcolor
"FSIN vec2 v_uv;\n"
"uniform sampler2D tex0;\n"
"uniform sampler2D tex1;\n"
"void main()\n"
"{\n"
"	vec4 bphos1 = vec4(0.24, 0.667, 0.969, 1.0);\n"
"	vec4 yphos1 = 0.9*vec4(0.475, 0.8, 0.243, 1.0);\n"
"	vec4 yphos2 = 0.975*vec4(0.494, 0.729, 0.118, 0.0);\n"

"	vec2 uv = vec2(v_uv.x, v_uv.y);\n"
"	vec4 white = texture2D(tex0, uv);\n"
"	vec4 yellow = texture2D(tex1, uv);\n"
"	vec4 yel = mix(yphos2, yphos1, yellow.x);\n"
"	float a = 0.663 * (yel.a + (1.0-cos(3.141569*yel.a))/2.0)/2.0;\n"
"	vec4 color = bphos1*white.x*white.z + yel*a;\n"
output
"}\n";

void
texDefaults(void)
{
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void
makeFBO(GLuint *fbo, GLuint *tex)
{
	glGenTextures(1, tex);
	glBindTexture(GL_TEXTURE_2D, *tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, BWIDTH, BHEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, nil);
	texDefaults();
	glGenFramebuffers(1, fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
}

void
initGL(void)
{
	GLint vs = compileshader(GL_VERTEX_SHADER, vs_src);
	GLint point_vs = compileshader(GL_VERTEX_SHADER, point_vs_src);
	GLint fs = compileshader(GL_FRAGMENT_SHADER, fs_src);
	GLint point_fs = compileshader(GL_FRAGMENT_SHADER, point_fs_src);
	GLint excite_fs = compileshader(GL_FRAGMENT_SHADER, excite_fs_src);
	GLint combine_fs = compileshader(GL_FRAGMENT_SHADER, combine_fs_src);
	program = linkprogram(fs, vs);
	point_program = linkprogram(point_fs, point_vs);
	excite_program = linkprogram(excite_fs, vs);
	combine_program = linkprogram(combine_fs, vs);

	glGenTextures(1, &gltex);
	glBindTexture(GL_TEXTURE_2D, gltex);
	texDefaults();

	makeFBO(&whiteFBO, &whiteTex);
	makeFBO(&yellowFBO[0], &yellowTex[0]);
	makeFBO(&yellowFBO[1], &yellowTex[1]);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);


	struct Vertex {
		float x, y;
		float u, v;
	} screenquad[] = {
		{ -1.0f, -1.0f,		0.0f, 0.0f },
		{ 1.0f, -1.0f,		1.0f, 0.0f },
		{ 1.0f, 1.0f,		1.0f, 1.0f },

		{ -1.0f, -1.0f,		0.0f, 0.0f },
		{ 1.0f, 1.0f,		1.0f, 1.0f },
		{ -1.0f, 1.0f,		0.0f, 1.0f },
	};
	GLuint stride = sizeof(struct Vertex);
//	glGenVertexArrays(1, &vao);
//	glBindVertexArray(vao);
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(screenquad), screenquad, GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, 0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(uintptr_t)offsetof(struct Vertex, u));
}

uint32 screenmodes[2] = { 0, SDL_WINDOW_FULLSCREEN_DESKTOP };
int fullscreen;

void
keydown(SDL_Keysym keysym)
{
	if(keysym.scancode == SDL_SCANCODE_F11){
		fullscreen = !fullscreen;
		SDL_SetWindowFullscreen(window, screenmodes[fullscreen]);
	}
	if(keysym.scancode == SDL_SCANCODE_ESCAPE)
		exit(0);

	switch(keysym.scancode) {
	case SDL_SCANCODE_UP:
		sizefoo += 0.0001f;
		printf("sz %g\n", sizefoo);
		break;
	case SDL_SCANCODE_DOWN:
		sizefoo -= 0.0001f;
		printf("sz %g\n", sizefoo);
		break;

	case SDL_SCANCODE_LEFT:
		intfoo -= 0.01f;
		printf("int %g\n", intfoo);
		break;
	case SDL_SCANCODE_RIGHT:
		intfoo += 0.01f;
		printf("int %g\n", intfoo);
		break;

	case SDL_SCANCODE_S:
		scalefoo = (scalefoo+1)%3;
		break;
	case SDL_SCANCODE_R:
		sizefoo = 0.005f;
		intfoo = 1.0f;
		scalefoo = 0;
		break;

	case SDL_SCANCODE_X:
		xxfoo = (xxfoo+1)%9;
		break;
	}
}

void
process(int frmtime)
{
	Point *p;
	int i, n, idx;

	/* age */
	n = 0;
	for(i = 0; i < npoints; i++) {
		p = &points[i];
		p->time += frmtime;
		if(p->time < 200000)
			points[idx = n++] = *p;
		else
			idx = -1;
		indices[p->y*1024 + p->x] = idx;
	}
	npoints = n;

	/* add new points */
	for(i = 0; i < nnewpoints; i++) {
		Point *np = &newpoints[i];
		idx = indices[np->y*1024 + np->x];
		if(idx < 0) {
			idx = npoints++;
			indices[np->y*1024 + np->x] = idx;
		}
		p = &points[idx];
		p->x = np->x;
		p->y = np->y;
		p->i = np->i;
		p->time = frmtime - np->time;
	}
	nnewpoints = 0;
}

//#define SAVELIST

void*
readthread(void *args)
{
	uint32 cmd;
	uint64 time;
	uint64 frmtime = 33333;
	int x, y, intensity, dt;

        SDL_Event ev;
        SDL_memset(&ev, 0, sizeof(SDL_Event));
        ev.type = userevent; 

#ifdef SAVELIST
	static uint32 displist[1024];
	int ndisp = 0;
	FILE *f = fopen("displist.dat", "wb");
#endif

	time = 0;
        while(readn(netfd, &cmd, 4) == 0){
		x = cmd&01777;
		y = cmd>>10 & 01777;
		intensity = cmd>>20 & 7;
		dt = cmd>>23;
		time += dt;

#ifdef SAVELIST
		displist[ndisp++] = cmd;
		if(ndisp == 1024) {
			fwrite(displist, 4, ndisp, f);
			ndisp = 0;
		}
#endif

		if(x || y || intensity) {
			Point *np = &newpoints[nnewpoints++];
			np->x = x>>scalefoo;
			np->y = y>>scalefoo;
			np->i = intensity;
if(xxfoo != 8) np->i = xxfoo;
			np->time = time;
		}

		// we hope draw is finished before we decide to flip again
		// 30fps should be doable
		if(time > frmtime) {
			time -= frmtime;
			process(frmtime);
			SDL_PushEvent(&ev);
		}
	}
	exit(0);
}

int penx;
int peny;
int pendown;

void
updatepen(void)
{
	uint32 cmd;
	cmd = 0xFF<<24;
	cmd |= pendown << 20;
// TODO: scaling
	cmd |= penx << 10;
	cmd |= 1023-peny;
	write(netfd, &cmd, 4);
//	printf("%d %d %d\n", penx, peny, pendown);
}

void
usage(void)
{
	fprintf(stderr, "usage: %s [-p port] [server]\n", argv0);
	exit(0);
}

int
main(int argc, char *argv[])
{
	pthread_t th;
	SDL_Event event;
	int running;
	int port;

	port = 3400;
	ARGBEGIN{
	case 'p':
		port = atoi(EARGF(usage()));
		break;
	}ARGEND;

	if(argc > 0)
		netfd = dial(argv[0], port);
	else
		netfd = serve1(port);
	if(netfd < 0)
		return 1;

	SDL_Init(SDL_INIT_EVERYTHING);

#ifdef GLES
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	window = SDL_CreateWindow("P7 sim", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, BWIDTH, BHEIGHT, window_flags);
	if(window == nil) {
		fprintf(stderr, "can't create window\n");
		return 1;
	}
	SDL_GLContext gl_context = SDL_GL_CreateContext(window);
	SDL_GL_MakeCurrent(window, gl_context);
	SDL_GL_SetSwapInterval(1); // Enable vsync

	for(int i = 0; i < 1024*1024; i++)
		indices[i] = -1;

//	gladLoadGL();
	gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress);

	initGL();

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
        userevent = SDL_RegisterEvents(1);

	pthread_create(&th, nil, readthread, nil);

	running = 1;
	while(running){
		while(SDL_PollEvent(&event)) {
			switch(event.type) {
			case SDL_TEXTINPUT:
//				textinput(event.text.text);
				break;
			case SDL_KEYDOWN:
//				keydown(event.key.keysym, event.key.repeat);
				keydown(event.key.keysym);
				break;
			case SDL_KEYUP:
//				keyup(event.key.keysym);
				break;

			case SDL_MOUSEMOTION:
				penx = event.motion.x;
				peny = event.motion.y;
				if(pendown)
					updatepen();
				break;
			case SDL_MOUSEBUTTONDOWN:
				if(event.button.button == 1)
					pendown = 1;
				updatepen();
				break;
			case SDL_MOUSEBUTTONUP:
				if(event.button.button == 1)
					pendown = 0;
				updatepen();
				break;

			case SDL_QUIT:
				running = 0;
				break;

			case SDL_USEREVENT:
				draw();
				break;

			case SDL_WINDOWEVENT:
				switch(event.window.event){
				case SDL_WINDOWEVENT_CLOSE:
					running = 0;
					break;
				case SDL_WINDOWEVENT_MOVED:
				case SDL_WINDOWEVENT_ENTER:
				case SDL_WINDOWEVENT_LEAVE:
				case SDL_WINDOWEVENT_FOCUS_GAINED:
				case SDL_WINDOWEVENT_FOCUS_LOST:
#ifdef SDL_WINDOWEVENT_TAKE_FOCUS
				case SDL_WINDOWEVENT_TAKE_FOCUS:
#endif
					break;
				}
			}
		}
//usleep(30000);
	}

	SDL_GL_DeleteContext(gl_context);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
