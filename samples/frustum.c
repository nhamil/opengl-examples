/* Copyright (c) 2014-2015 Scott Kuhl. All rights reserved.
 * License: This code is licensed under a 3-clause BSD license. See
 * the file named "LICENSE" for a full copy of the license.
 */

/** @file Implements a dynamic view frustum where the camera is
 * controlled by the keyboard.
 *
 * @author Scott Kuhl
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <GL/glew.h>
#ifdef FREEGLUT
#include <GL/freeglut.h>
#else
#include <GLUT/glut.h>
#endif

#include "kuhl-util.h"
#include "vecmat.h"
#include "dgr.h"
#include "projmat.h"
#include "viewmat.h"
GLuint program = 0; /**< id value for the GLSL program */

float camPos[3]  = {0,1.5,0}; /**< location of camera, 1.5 m eyeheight */

/** Position of screen in world coordinates:
 left, right, bottom, top, near, far */
const float screen[6] = { -2, 2, 0, 4, -1, -100 };

kuhl_geometry *modelgeom = NULL;

/** Called by GLUT whenever a key is pressed. */
void keyboard(unsigned char key, int x, int y)
{
	switch(key)
	{
		case 'q':
		case 'Q':
		case 27: // ASCII code for Escape key
			dgr_exit();
			exit(EXIT_SUCCESS);
			break;

		case 'a': // left
			camPos[0] -= .2;
			break;
		case 'd': // right
			camPos[0] += .2;
			break;
		case 'w': // up
			camPos[1] += .2;
			break;
		case 's': // down
			camPos[1] -= .2;
			break;
		case 'z': // toward screen
			camPos[2] -= .2;
			if(screen[4]-camPos[2] > -.2)
				camPos[2] = .2+screen[4];
			break;
		case 'x': // away from screen
			camPos[2] += .2;
			break;
	}
	printf("camera position: ");
	vec3f_print(camPos);

	
	/* Whenever any key is pressed, request that display() get
	 * called. */ 
	glutPostRedisplay();
}

/** Called by GLUT whenever the window needs to be redrawn. This
 * function should not be called directly by the programmer. Instead,
 * we can call glutPostRedisplay() to request that GLUT call display()
 * at some point. */
void display()
{
	/* If we are using DGR, send or receive data to keep multiple
	 * processes/computers synchronized. */
	dgr_update();

	/* Render the scene once for each viewport. Frequently one
	 * viewport will fill the entire screen. However, this loop will
	 * run twice for HMDs (once for the left eye and once for the
	 * right). */
	viewmat_begin_frame();
	for(int viewportID=0; viewportID<viewmat_num_viewports(); viewportID++)
	{
		viewmat_begin_eye(viewportID);

		/* Where is the viewport that we are drawing onto and what is its size? */
		int viewport[4]; // x,y of lower left corner, width, height
		viewmat_get_viewport(viewport, viewportID);
		/* Tell OpenGL the area of the window that we will be drawing in. */
		glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

		/* Clear the current viewport. Without glScissor(), glClear()
		 * clears the entire screen. We could call glClear() before
		 * this viewport loop---but in order for all variations of
		 * this code to work (Oculus support, etc), we can only draw
		 * after viewmat_begin_eye(). */
		glScissor(viewport[0], viewport[1], viewport[2], viewport[3]);
		glEnable(GL_SCISSOR_TEST);
		glClearColor(.2,.2,.2,0); // set clear color to grey
		glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
		glDisable(GL_SCISSOR_TEST);
		glEnable(GL_DEPTH_TEST); // turn on depth testing
		kuhl_errorcheck();

		/* Get the view matrix and the projection matrix */
		float viewMat[16], perspective[16];
		// Instead of using viewmat_*() functions to get view and
		// projection matrices, generate our own.
		// viewmat_get(viewMat, perspective, viewportID);
		const float camLook[3] = { camPos[0], camPos[1], camPos[2]-1 }; // look straight ahead
		const float camUp[3] = { 0, 1, 0 };
		mat4f_lookatVec_new(viewMat, camPos, camLook, camUp);
		mat4f_frustum_new(perspective,
		                  screen[0]-camPos[0], screen[1]-camPos[0], // left, right
		                  screen[2]-camPos[1], screen[3]-camPos[1], // bottom, top
		                  screen[4]-camPos[2], screen[5]-camPos[2]); // near, far

		
		/* Calculate an angle to rotate the
		 * object. glutGet(GLUT_ELAPSED_TIME) is the number of
		 * milliseconds since glutInit() was called. */
		int count = glutGet(GLUT_ELAPSED_TIME) % 10000; // get a counter that repeats every 10 seconds
		float angle = count / 10000.0 * 360; // rotate 360 degrees every 10 seconds
		/* Make sure all computers/processes use the same angle */
		dgr_setget("angle", &angle, sizeof(GLfloat));

		/* Combine the scale and rotation matrices into a single model matrix.
		   modelMat = scaleMat * rotateMat
		*/
		float modelMat[16];
		mat4f_rotateAxis_new(modelMat, 90, 0, 1, 0);
		//mat4f_identity(modelMat);

		/* Construct a modelview matrix: modelview = viewMat * modelMat */
		float modelview[16];
		mat4f_mult_mat4f_new(modelview, viewMat, modelMat);

		/* Tell OpenGL which GLSL program the subsequent
		 * glUniformMatrix4fv() calls are for. */
		kuhl_errorcheck();
		glUseProgram(program);
		kuhl_errorcheck();
		
		/* Send the perspective projection matrix to the vertex program. */
		glUniformMatrix4fv(kuhl_get_uniform("Projection"),
		                   1, // number of 4x4 float matrices
		                   0, // transpose
		                   perspective); // value
		/* Send the modelview matrix to the vertex program. */
		glUniformMatrix4fv(kuhl_get_uniform("ModelView"),
		                   1, // number of 4x4 float matrices
		                   0, // transpose
		                   modelview); // value
		GLuint renderStyle = 2;
		glUniform1i(kuhl_get_uniform("renderStyle"), renderStyle);
		kuhl_errorcheck();
		/* Draw the geometry using the matrices that we sent to the
		 * vertex programs immediately above */
		kuhl_geometry_draw(modelgeom);

		glUseProgram(0); // stop using a GLSL program.

	} // finish viewport loop
	viewmat_end_frame();

	/* Check for errors. If there are errors, consider adding more
	 * calls to kuhl_errorcheck() in your code. */
	kuhl_errorcheck();

	/* Ask GLUT to call display() again. We shouldn't call display()
	 * ourselves recursively because it will not leave time for GLUT
	 * to call other callback functions for when a key is pressed, the
	 * window is resized, etc. */
	glutPostRedisplay();
}


int main(int argc, char** argv)
{
	/* Set up our GLUT window */
	glutInit(&argc, argv);
	glutInitWindowSize(512, 512);
	/* Ask GLUT to for a double buffered, full color window that
	 * includes a depth buffer */
#ifdef FREEGLUT
	glutSetOption(GLUT_MULTISAMPLE, 4); // set msaa samples; default to 4
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH | GLUT_MULTISAMPLE);
	glutInitContextVersion(3,2);
	glutInitContextProfile(GLUT_CORE_PROFILE);
#else
	glutInitDisplayMode(GLUT_3_2_CORE_PROFILE | GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH | GLUT_MULTISAMPLE);
#endif
	glutCreateWindow(argv[0]); // set window title to executable name
	glEnable(GL_MULTISAMPLE);

	/* Initialize GLEW */
	glewExperimental = GL_TRUE;
	GLenum glewError = glewInit();
	if(glewError != GLEW_OK)
	{
		fprintf(stderr, "Error initializing GLEW: %s\n", glewGetErrorString(glewError));
		exit(EXIT_FAILURE);
	}
	/* When experimental features are turned on in GLEW, the first
	 * call to glGetError() or kuhl_errorcheck() may incorrectly
	 * report an error. So, we call glGetError() to ensure that a
	 * later call to glGetError() will only see correct errors. For
	 * details, see:
	 * http://www.opengl.org/wiki/OpenGL_Loading_Library */
	glGetError();

	// setup callbacks
	glutDisplayFunc(display);
	glutKeyboardFunc(keyboard);

	/* Compile and link a GLSL program composed of a vertex shader and
	 * a fragment shader. */
	program = kuhl_create_program("assimp.vert", "assimp.frag");
	modelgeom = kuhl_load_model("models/dabrovic-sponza/sponza.obj", NULL, program, NULL);
	if(modelgeom == 0)
	{
		msg(FATAL, "Dabrovic sponza scene is required for this example. If needed, modify the filename of the model in main().");
		msg(FATAL, "http://graphics.cs.williams.edu/data/meshes.xml");
		exit(EXIT_FAILURE);
	}


	/* Good practice: Unbind objects until we really need them. */
	glUseProgram(0);

	dgr_init();     /* Initialize DGR based on environment variables. */
	//projmat_init(); /* Figure out which projection matrix we should use based on environment variables */
	//viewmat_init(camPos, camLook, camUp);
	
	/* Tell GLUT to start running the main loop and to call display(),
	 * keyboard(), etc callback methods as needed. */
	glutMainLoop();
    /* // An alternative approach:
    while(1)
       glutMainLoopEvent();
    */

	exit(EXIT_SUCCESS);
}
