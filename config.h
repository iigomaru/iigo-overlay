/* The width of the overlay in pixels. */
#define WIDTH 80
/* The height of the overlay in pixels. */
#define HEIGHT 12

/* The in game width in meters */
#define INGAMEWIDTH .14

#define TAU 6.28318530718

/* The settings for the positioning of the overlay relative to the right controller */
#define XANGLE 45.0  /* The default value is 45.0  */
#define YANGLE 90.0  /* The default value is 90.0  */
#define ZANGLE -90.0 /* The default value is -90.0 */
#define TRANS1 0.05  /* The default value is 0.05  */
#define TRANS2 -0.05 /* The default value is -0.05 */
#define TRANS3 0.24  /* The default value is 0.24  */

/* The settings for the positioning of the overlay relative to the hmd */
#define XANGLE_HMD -15.0 /* The default value is -15.0 */
#define YANGLE_HMD 0.0   /* The default value is 0.0   */
#define ZANGLE_HMD 180.0 /* The default value is 180.0 */
#define TRANS1_HMD 0.0   /* The default value is 0.0   */
#define TRANS2_HMD -0.2  /* The default value is -0.2  */
#define TRANS3_HMD -0.5  /* The default value is -0.5  */

/* OSC ports */
#define OSC_LOCALHOST "127.0.0.1"
#define OSC_INPUTPORT_MAIN 9001
#define OSC_OUTPUTPORT_MAIN 9000
#define OSC_OUTPUTPORT_BUTT 9069