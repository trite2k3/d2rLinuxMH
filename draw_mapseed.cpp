#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>

#include <GL/glew.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Global variables
std::vector<std::vector<int>> map_data_rows;
std::vector<json> objects;  // To store map objects (NPCs, waypoints, etc.)
int map_width = 0;
int map_height = 0;

// Function prototypes
json load_map_data(const std::string& file_path);
void parse_map_data(const json& map_level);
void renderScene(Display* display, Window window);
void draw_objects();
void reshape(int width, int height);
void init_opengl();
void make_window_click_through(Display* display, Window window);
void make_window_transparent(Display* display, Window window);
void make_window_always_on_top(Display* display, Window window);
void set_window_properties(Display* display, Window window);

// Main function
int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: ./draw_mapseed /path/to/map_data.json" << std::endl;
        exit(1);
    }

    // Load the map data from JSON
    std::string file_path = argv[1];
    json map_data_json = load_map_data(file_path);

    bool map_found = false;
    for (const auto& level : map_data_json["levels"]) {
        if (level.contains("type") && level["type"] == "map") {
            parse_map_data(level);
            map_found = true;
            break;
        }
    }

    if (!map_found) {
        std::cerr << "No 'map' type found in the provided JSON data." << std::endl;
        exit(1);
    }

    // Set up X11 and GLX
    Display* display = XOpenDisplay(NULL);
    if (!display) {
        std::cerr << "Failed to open X display" << std::endl;
        exit(1);
    }

    int screen = DefaultScreen(display);
    int depth = 32;

    // Find a 32-bit visual with alpha channel
    XVisualInfo visual_info_template;
    int visual_info_count;
    visual_info_template.screen = screen;
    visual_info_template.depth = depth;
    visual_info_template.c_class = TrueColor;
    XVisualInfo* visual_list = XGetVisualInfo(display, VisualScreenMask | VisualDepthMask | VisualClassMask, &visual_info_template, &visual_info_count);

    if (!visual_list || visual_info_count == 0) {
        std::cerr << "No 32-bit TrueColor visual with alpha channel found." << std::endl;
        exit(1);
    }

    XVisualInfo* visual = &visual_list[0];

    // Verify the visual supports alpha channel
    XRenderPictFormat *pictFormat = XRenderFindVisualFormat(display, visual->visual);
    if (!pictFormat || pictFormat->direct.alphaMask == 0) {
        std::cerr << "Visual does not support alpha channel." << std::endl;
        exit(1);
    }

    // Create a colormap with the visual
    Colormap colormap = XCreateColormap(display, RootWindow(display, visual->screen), visual->visual, AllocNone);

    // Set window attributes
    XSetWindowAttributes swa;
    swa.colormap = colormap;
    swa.border_pixel = 0;
    swa.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask;
    swa.background_pixmap = None;
    swa.background_pixel = 0;  // Set background to transparent

    // Create a borderless fullscreen window
    Window root = RootWindow(display, visual->screen);
    Window window = XCreateWindow(display, root, 0, 0, 1920, 1080, 0, visual->depth, InputOutput,
                                visual->visual, CWColormap | CWBorderPixel | CWEventMask | CWBackPixel, &swa);

    // Set the window to be borderless fullscreen
    XMapWindow(display, window);
    XStoreName(display, window, "Map Viewer with Objects");

    // Make the window transparent and click-through
    make_window_transparent(display, window);
    make_window_click_through(display, window);
    make_window_always_on_top(display, window);
    set_window_properties(display, window);  // Additional properties

    // Create GLX context
    GLXContext glc = glXCreateContext(display, visual, NULL, True);
    if (!glc) {
        std::cerr << "Failed to create GLX context." << std::endl;
        exit(1);
    }

    glXMakeCurrent(display, window, glc);


    // Initialize GLEW
    GLenum err = glewInit();
    if (GLEW_OK != err) {
        std::cerr << "Error initializing GLEW: " << glewGetErrorString(err) << std::endl;
        exit(1);
    }

    // Initialize OpenGL settings
    init_opengl();

    // Main event loop
    bool running = true;
    while (running) {
        while (XPending(display)) {
            XEvent xev;
            XNextEvent(display, &xev);

            if (xev.type == Expose) {
                renderScene(display, window);  // Pass display and window here
                glXSwapBuffers(display, window);
            } else if (xev.type == ConfigureNotify) {
                reshape(xev.xconfigure.width, xev.xconfigure.height);
            } else if (xev.type == KeyPress) {
                running = false;
            }
        }

        // Render the scene and swap buffers
        renderScene(display, window);  // Pass display and window here
        glXSwapBuffers(display, window);
    }

    // Clean up
    glXMakeCurrent(display, None, NULL);
    glXDestroyContext(display, glc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return 0;
}

// Load map data from the JSON file
json load_map_data(const std::string& file_path) {
    std::ifstream f(file_path);
    if (!f) {
        std::cerr << "Failed to open file: " << file_path << std::endl;
        exit(1);
    }
    json data;
    try {
        f >> data;
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse JSON: " << e.what() << std::endl;
        exit(1);
    }
    return data;
}

void parse_map_data(const json& map_level) {
    if (map_level.find("map") == map_level.end()) {
        std::cerr << "No 'map' key found in the map level." << std::endl;
        exit(1);
    }

    // Extract the map data
    map_data_rows = map_level["map"].get<std::vector<std::vector<int>>>();

    // Extract objects (NPCs, waypoints, etc.)
    if (map_level.contains("objects")) {
        objects = map_level["objects"].get<std::vector<json>>();
    }

    // Determine map dimensions
    map_height = map_data_rows.size();
    for (const auto& row : map_data_rows) {
        int sum = 0;
        for (int val : row) {
            sum += val;
        }
        if (sum > map_width) {
            map_width = sum;
        }
    }
}

// OpenGL initialization for transparency and blending
void init_opengl() {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);  // Disable depth testing
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
}

// Rendering the map with black walls and white interiors
void renderScene(Display* display, Window window) {
    // Clear the screen with a transparent background
    glClearColor(0, 0, 0, 0);  // Ensure clear color is fully transparent
    glClear(GL_COLOR_BUFFER_BIT);

    // Clear the screen with a transparent background
    //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glPushMatrix();  // Save the current matrix

    // Calculate the map center based on its width and height
    float center_x = map_width / 2.0f;
    float center_y = map_height / 2.0f;

    // First, translate to the correct position in the normal (non-rotated) coordinate system
    glTranslatef(-center_x, -center_y, 0);  // Move the map to its correct center position before rotating

    // Apply the vertical flip by scaling the Y-axis by -1
    glScalef(1.0f, -1.0f, 1.0f);  // This flips the Y-axis

    // Apply an additional translation to move the map upwards
    float upward_offset = -200.0f;  // Adjust this value to move the map higher
    glTranslatef(0.0f, upward_offset, 0.0f);  // Shift the map upwards

    // Apply the 45-degree rotation (around the origin)
    glTranslatef(center_x, center_y, 0);  // Translate to the center of the map
    glRotatef(45.0f, 0.0f, 0.0f, 1.0f);  // Rotate by 45 degrees around the Z-axis
    glTranslatef(-center_x, -center_y, 0);  // Translate back to the original map position

    // Now draw the map
    for (int y = 0; y < map_height; ++y) {
        const std::vector<int>& row = map_data_rows[y];
        int x = 0;
        bool fill = true;

        for (int offset : row) {
            if (!fill) {
                // Opaque white for the map itself
                glColor4f(1.0f, 1.0f, 1.0f, 0.2f);

                // Draw the quad for unfilled areas (the map itself)
                glBegin(GL_QUADS);
                glVertex2i(x, y);
                glVertex2i(x + offset, y);
                glVertex2i(x + offset, y + 1);
                glVertex2i(x, y + 1);
                glEnd();
            }
            // Move to the next x position
            x += offset;

            // Toggle between filled and unfilled
            fill = !fill;
        }
    }

    // Draw objects on top of the map (if needed)
    draw_objects();

    glPopMatrix();  // Restore the matrix state

    // Swap the buffers to make sure rendering is updated
    glXSwapBuffers(display, window);
}


void reshape(int width, int height) {
    glViewport(0, 0, width, height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    // Adjust these values to zoom out the view and fit the rotated map
    float aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
    float zoom_factor = 1.4f; // Increase this value to zoom out further

    // Adjust the ortho parameters to fit the map and account for rotation
    if (aspect_ratio > 1.0f) {
        glOrtho(-map_width * zoom_factor * aspect_ratio / 2, map_width * zoom_factor * aspect_ratio / 2,
                -map_height * zoom_factor / 2, map_height * zoom_factor / 2, -1, 1);
    } else {
        glOrtho(-map_width * zoom_factor / 2, map_width * zoom_factor / 2,
                -map_height * zoom_factor / aspect_ratio / 2, map_height * zoom_factor / aspect_ratio / 2, -1, 1);
    }

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void draw_objects() {
    for (const auto& object : objects) {
        int x = object["x"];
        int y = object["y"];

        if (object.contains("op") && object["op"] == 23) {
            glColor4f(0.0f, 0.0f, 1.0f, 0.7f);  // Blue waypoints, 70% opaque
        } else if (object.contains("type") && object["type"] == "exit") {
            if (object.contains("id") && object["id"] == 102) {
                glColor4f(0.0f, 1.0f, 0.0f, 0.7f);  // Green for exit with id 102, 70% opaque
            } else if (object.contains("id") && object["id"] == 100) {
                glColor4f(1.0f, 0.0f, 0.0f, 0.7f);  // Red for exit with id 100, 70% opaque
            } else {
                glColor4f(1.0f, 1.0f, 0.0f, 0.7f);  // Yellow for other exits, 70% opaque
            }
        } else {
            continue;
        }

        glBegin(GL_QUADS);
        glVertex2i(x - 6, y - 6);
        glVertex2i(x + 6, y - 6);
        glVertex2i(x + 6, y + 6);
        glVertex2i(x - 6, y + 6);
        glEnd();
    }
}

// Window utilities
void make_window_transparent(Display* display, Window window) {
    // No need to set window-level opacity since OpenGL handles it
}

void make_window_click_through(Display* display, Window window) {
    XRectangle rect;
    rect.x = 0;
    rect.y = 0;
    rect.width = 0;
    rect.height = 0;
    XShapeCombineRectangles(display, window, ShapeInput, 0, 0, &rect, 0, ShapeSet, Unsorted);
}

void make_window_always_on_top(Display* display, Window window) {
    Atom wmStateAbove = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
    Atom wmState = XInternAtom(display, "_NET_WM_STATE", False);

    XClientMessageEvent event = {};
    event.type = ClientMessage;
    event.window = window;
    event.message_type = wmState;
    event.format = 32;
    event.data.l[0] = 1;  // _NET_WM_STATE_ADD
    event.data.l[1] = wmStateAbove;
    event.data.l[2] = 0;
    event.data.l[3] = 1;
    event.data.l[4] = 0;

    XSendEvent(display, DefaultRootWindow(display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, (XEvent*)&event);
}

void set_window_properties(Display* display, Window window) {
    Atom windowType = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom typeNormal = XInternAtom(display, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    XChangeProperty(display, window, windowType, XA_ATOM, 32, PropModeReplace, (unsigned char*)&typeNormal, 1);
}
