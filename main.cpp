
#include <GL/freeglut.h>
#include <chrono>  // time measure
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <map>
#include <cmath> //

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace std::chrono;

// ------------------ CONFIG ------------------
const int WINDOW_W = 1000;
const int WINDOW_H = 900;

const int SLOT_W = 280;
const int SLOT_H = 280;
const int GRID_COLS = 3;
const int GRID_ROWS = 2;
const int GAP_X = 50;
const int GAP_Y = 80;

// Billing rules
const int MAX_SECONDS = 30;                 
const double MIN_FIRST_MIN_TK = 100.0;    
const double PENALTY_PER_EXTRA_SEC = 1.0; 

// UI
const double MESSAGE_DISPLAY_SEC = 5.0; 

const bool FLIP_X_TEXTURE = false;
const bool FLIP_Y_TEXTURE = false;

// ---------------- Helpers ------------------
struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

struct TextureInfo {
    GLuint id = 0;
    int w = 0;
    int h = 0;
};

// ----------------- Vehicle -----------------
class Vehicle {
public:
    enum Type { NONE = 0, CAR = 1, BIKE = 2, TRUCK = 3 };
    Vehicle(): type(NONE), texId(0), texW(0), texH(0), name("None") {}
    Vehicle(Type t, GLuint id, int w, int h, const std::string &n)
        : type(t), texId(id), texW(w), texH(h), name(n) {}
    Type type;
    GLuint texId;
    int texW, texH;
    std::string name;
};

// ----------------- Slot -----------------
class Slot {
public:
    Slot() {}
    Slot(int x_, int y_, int w_, int h_) :
        x(x_), y(y_), w(w_), h(h_), parked(false), vehicle(), overstay(false) {}
    int x,y,w,h;
    bool parked;
    Vehicle vehicle;
    time_point<steady_clock> start_time;
    bool overstay;

    bool contains(int mx, int my) const { return (mx >= x && mx <= x + w && my >= y && my <= y + h); }

    double elapsedSeconds() const { 
        if (!parked) return 0.0;
        auto now = steady_clock::now();
        return duration<double>(now - start_time).count();
    }

    void park(const Vehicle &v) {
        parked = true;
        vehicle = v;
        start_time = steady_clock::now();
        overstay = false;
    }

    double computeBill() const {
        if (!parked) return 0.0;
        double elapsed = elapsedSeconds();
        if (elapsed <= MAX_SECONDS) return MIN_FIRST_MIN_TK;
        double extra = std::floor(elapsed - MAX_SECONDS);
        return MIN_FIRST_MIN_TK + extra * PENALTY_PER_EXTRA_SEC;
    }

    double removeAndGetBill() {
        double bill = computeBill();
        parked = false;
        vehicle = Vehicle();
        overstay = false;
        return bill;
    }
};

// ----------------- ParkingManager -----------------
class ParkingManager {
public:
    ParkingManager(): selectedSlot(-1), showSelectionMenu(false),
                      menuX(0), menuY(0), showConfirm(false), confirmSlot(-1),
                      totalCollected(0.0), hoverSlot(-1), lastMessage(""), lastMsgTime() {
        initSlots();  //MAKE The slot
    }

    void initSlots() {
        slots.clear();
        int totalW = GRID_COLS * SLOT_W + (GRID_COLS - 1) * GAP_X;
        int totalH = GRID_ROWS * SLOT_H + (GRID_ROWS - 1) * GAP_Y;
        int startX = (WINDOW_W - totalW) / 2;
        int startY = (WINDOW_H - totalH) / 2;
        for (int r = 0; r < GRID_ROWS; ++r) {
            for (int c = 0; c < GRID_COLS; ++c) {
                int sx = startX + c * (SLOT_W + GAP_X);
                int sy = startY + r * (SLOT_H + GAP_Y);
                slots.emplace_back(sx, sy, SLOT_W, SLOT_H);
            }
        }
    }

    void setTextures(const std::map<std::string, TextureInfo> &t) {
        textures = t;
        vehicles[Vehicle::CAR] = Vehicle(Vehicle::CAR, textures.at("car").id, textures.at("car").w, textures.at("car").h, "Car");
        vehicles[Vehicle::BIKE] = Vehicle(Vehicle::BIKE, textures.at("bike").id, textures.at("bike").w, textures.at("bike").h, "Bike");
        vehicles[Vehicle::TRUCK] = Vehicle(Vehicle::TRUCK, textures.at("truck").id, textures.at("truck").w, textures.at("truck").h, "Truck");
    }

    void render() {
        drawHUDBar();

        for (size_t i = 0; i < slots.size(); ++i) drawSlotBackground(i);
        for (size_t i = 0; i < slots.size(); ++i) drawSlotVehicle(i);
        for (size_t i = 0; i < slots.size(); ++i) drawSlotText(i);

        if (showSelectionMenu) renderSelectionMenu();
        if (showConfirm) renderConfirmDialog();
        renderTransientMessage();

        if (hoverSlot >= 0 && hoverSlot < (int)slots.size()) {
            const Slot &s = slots[hoverSlot];
            drawRectBorder(s.x - 2, s.y - 2, s.w + 4, s.h + 4, 3.0f, 0.0f, 0.6f, 0.0f);
        }
    }

    void onMouseClick(int mx, int my, int button, int state) {
        if (button != GLUT_LEFT_BUTTON || state != GLUT_DOWN) return;

        if (showConfirm) {
            if (handleConfirmClick(mx, my)) return;
            showConfirm = false; confirmSlot = -1;
            return;
        }

        if (showSelectionMenu) {
            if (handleSelectionClick(mx, my)) return;
            showSelectionMenu = false; selectedSlot = -1;
            return;
        }

        for (size_t i = 0; i < slots.size(); ++i) {
            if (slots[i].contains(mx,my)) {
                if (!slots[i].parked) {
                    selectedSlot = (int)i;
                    int boxW = 3 * 92 + 2 * 12;
                    int bx = slots[i].x + (slots[i].w - boxW) / 2;
                    int by = slots[i].y + slots[i].h + 10;
                    if (by + 140 > WINDOW_H) by = slots[i].y - 140;
                    menuX = std::max(8, bx);
                    menuY = std::max(8, by);
                    showSelectionMenu = true;
                } else {
                    showConfirm = true;
                    confirmSlot = (int)i;
                }
                return;
            }
        }
    }

    void onMouseMove(int mx, int my) {
        hoverSlot = -1;
        for (size_t i = 0; i < slots.size(); ++i) {
            if (slots[i].contains(mx,my)) { hoverSlot = (int)i; break; }
        }
    }

    void update() {
        for (auto &s: slots) {
            if (s.parked && !s.overstay) {
                double elapsed = s.elapsedSeconds();
                if (elapsed > MAX_SECONDS) s.overstay = true;
            }
        }
    }

private:
    std::vector<Slot> slots;
    std::map<std::string, TextureInfo> textures;
    std::map<Vehicle::Type, Vehicle> vehicles;
    int selectedSlot;
    bool showSelectionMenu;
    int menuX, menuY;

    bool showConfirm;
    int confirmSlot;
    Rect confirmYesRect, confirmNoRect;

    std::string lastMessage;
    time_point<steady_clock> lastMsgTime;
    double totalCollected;
    int hoverSlot;

    void drawHUDBar() {
        drawRect(0,0,WINDOW_W,80,0.95f,0.96f,0.99f);
        drawRectBorder(0,0,WINDOW_W,80,2.5f);
        drawStringAt("Left Click = Park | Click occupied = Remove (confirmation) | First 1 min = 100 Tk | After 1 min = +1 Tk/sec", 12, 28, GLUT_BITMAP_HELVETICA_12);

        int parkedCount = 0;
        for (auto &s: slots) if (s.parked) ++parkedCount;
        std::ostringstream st, tk;
        st << "Parked: " << parkedCount << " / " << slots.size();
        tk << "Collected: " << std::fixed << std::setprecision(0) << totalCollected << " Tk";
        drawStringAt(st.str(), WINDOW_W - 320, 30, GLUT_BITMAP_HELVETICA_12);
        drawStringAt(tk.str(), WINDOW_W - 320, 54, GLUT_BITMAP_HELVETICA_12);
    }

    void drawSlotBackground(size_t i) {
        const Slot &s = slots[i];
        if (!s.parked) drawRect(s.x, s.y, s.w, s.h, 0.94f, 0.98f, 0.94f);
        else if (s.overstay) drawRect(s.x, s.y, s.w, s.h, 1.0f, 0.78f, 0.78f);
        else drawRect(s.x, s.y, s.w, s.h, 0.97f, 0.97f, 0.97f);
        drawRectBorder(s.x, s.y, s.w, s.h);
    }

    void drawSlotVehicle(size_t i) {
        const Slot &s = slots[i];
        if (!s.parked || s.vehicle.texId == 0) return;

        int bw = s.w - 20;
        int bh = s.h - 20;
        drawTexturedRect(s.vehicle.texId, s.vehicle.texW, s.vehicle.texH, s.x + 10, s.y + 10, bw, bh, FLIP_X_TEXTURE, FLIP_Y_TEXTURE);
    }

    void drawSlotText(size_t i) {
        const Slot &s = slots[i];
        std::ostringstream sn; sn << "S" << (i+1);
        drawStringAt(sn.str(), s.x + 8, s.y + 14, GLUT_BITMAP_HELVETICA_12);

        std::string timerText;
        if (!s.parked) timerText = "Empty";
        else {
            int elapsed = (int)std::floor(s.elapsedSeconds());
            int minutes = elapsed / 60;
            int seconds = elapsed % 60;
            std::ostringstream tt;
            tt << minutes << ":" << std::setw(2) << std::setfill('0') << seconds;
            if (s.overstay) {
                int extra = std::max(0, elapsed - MAX_SECONDS);
                tt << "  Over+" << extra << "s  Penalty: " << std::fixed << std::setprecision(0) << extra * PENALTY_PER_EXTRA_SEC << " Tk";
            }
            timerText = tt.str();
        }

        void* font = GLUT_BITMAP_HELVETICA_18;
        int textW = getBitmapTextWidth(timerText, font);
        int tx = s.x + (s.w - textW)/2;
        int ty = s.y - 18; 
        if (ty < 6) ty = s.y + s.h + 8;
        drawStringAt(timerText, tx, ty, font);
    }

    // ---------------- Selection Menu ----------------
    void renderSelectionMenu() {
        int boxW = 92, boxH = 120, pad = 12;
        int totalW = 3*boxW + 2*pad;
        drawRect(menuX - 8, menuY - 8, totalW + 16, boxH + 16, 0.98f, 0.98f, 1.0f);
        drawRectBorder(menuX - 8, menuY - 8, totalW + 16, boxH + 16);
        for (int i = 0; i < 3; ++i) {
            int bx = menuX + i * (boxW + pad);
            int by = menuY;
            drawRect(bx, by, boxW, boxH, 1.0f, 1.0f, 1.0f);
            drawRectBorder(bx, by, boxW, boxH);
            GLuint t = 0; int tw=0, th=0; std::string lab;
            if (i==0){ t=textures["car"].id; tw=textures["car"].w; th=textures["car"].h; lab="Car"; }
            if (i==1){ t=textures["bike"].id; tw=textures["bike"].w; th=textures["bike"].h; lab="Bike"; }
            if (i==2){ t=textures["truck"].id; tw=textures["truck"].w; th=textures["truck"].h; lab="Truck"; }
            if (t) drawTexturedRect(t, tw, th, bx + 8, by + 8, boxW - 16, boxH - 40, FLIP_X_TEXTURE, FLIP_Y_TEXTURE);
            drawStringAt(lab, bx + 10, by + boxH - 18, GLUT_BITMAP_HELVETICA_12);
        }
        drawStringAt("Choose Vehicle", menuX, menuY - 18, GLUT_BITMAP_HELVETICA_12);
    }

    // ---------- Confirmation Dialog ----------
    void renderConfirmDialog() {
        int dw=520, dh=150;
        int dx=(WINDOW_W-dw)/2, dy=(WINDOW_H-dh)/2;

        glEnable(GL_BLEND);
        glColor4f(0,0,0,0.35f);
        glBegin(GL_QUADS);
            glVertex2i(0,0); glVertex2i(WINDOW_W,0); glVertex2i(WINDOW_W,WINDOW_H); glVertex2i(0,WINDOW_H);
        glEnd();
        glDisable(GL_BLEND);

        drawRect(dx, dy, dw, dh, 1.0f,1.0f,1.0f);
        drawRectBorder(dx, dy, dw, dh);
        drawStringAt("Do you want to remove the vehicle from this slot?", dx+20, dy+40, GLUT_BITMAP_HELVETICA_18);

        if (confirmSlot>=0 && confirmSlot<(int)slots.size()) {
            const Slot &s=slots[confirmSlot];
            if (s.parked) {
                int elapsed=(int)std::floor(s.elapsedSeconds());
                int minutes = elapsed/60, seconds=elapsed%60;
                std::ostringstream info;
                info<<"Elapsed: "<<minutes<<":"<<std::setw(2)<<std::setfill('0')<<seconds;
                double bill = s.computeBill();
                info<<"   Estimated Bill: "<<std::fixed<<std::setprecision(0)<<bill<<" Tk";
                drawStringAt(info.str(), dx+20, dy+72, GLUT_BITMAP_HELVETICA_12);
            }
        }

        int bw=130, bh=48, spacing=40;
        int bx_yes = dx + (dw/2) - bw - spacing/2;
        int bx_no  = dx + (dw/2) + spacing/2;
        int by_btn = dy + dh - bh - 18;

        drawRect(bx_yes, by_btn, bw, bh, 0.85f,0.95f,0.85f);
        drawRectBorder(bx_yes, by_btn, bw, bh);
        drawStringAt("Yes", bx_yes + bw/2 - 12, by_btn + bh/2 + 6, GLUT_BITMAP_HELVETICA_18);

        drawRect(bx_no, by_btn, bw, bh, 0.95f,0.85f,0.85f);
        drawRectBorder(bx_no, by_btn, bw, bh);
        drawStringAt("No", bx_no + bw/2 - 8, by_btn + bh/2 + 6, GLUT_BITMAP_HELVETICA_18);

        confirmYesRect = {bx_yes, by_btn, bw, bh};
        confirmNoRect = {bx_no, by_btn, bw, bh};
    }

    bool handleSelectionClick(int mx,int my){
        int boxW=92,boxH=120,pad=12;
        for(int i=0;i<3;++i){
            int bx=menuX+i*(boxW+pad),by=menuY;
            Rect r{bx,by,boxW,boxH};
            if(r.contains(mx,my)){
                Vehicle::Type chosen = Vehicle::NONE;
                if(i==0) chosen = Vehicle::CAR;
                if(i==1) chosen = Vehicle::BIKE;
                if(i==2) chosen = Vehicle::TRUCK;
                if(chosen!=Vehicle::NONE && selectedSlot>=0 && selectedSlot<(int)slots.size()){
                    if(!slots[selectedSlot].parked) slots[selectedSlot].park(vehicles[chosen]);
                }
                showSelectionMenu=false; selectedSlot=-1;
                return true;
            }
        }
        return false;
    }

    bool handleConfirmClick(int mx,int my){
        if(confirmSlot<0 || confirmSlot>=(int)slots.size()){ showConfirm=false; confirmSlot=-1; return true;}
        if(confirmYesRect.contains(mx,my)){
            double bill=slots[confirmSlot].removeAndGetBill();
            totalCollected+=bill;
            std::ostringstream m; m<<"Slot "<<(confirmSlot+1)<<" removed. Bill: "<<std::fixed<<std::setprecision(0)<<bill<<" Tk";
            lastMessage = m.str(); lastMsgTime=steady_clock::now();
            std::cout<<lastMessage<<std::endl;
            showConfirm=false; confirmSlot=-1;
            return true;
        } else if(confirmNoRect.contains(mx,my)){ showConfirm=false; confirmSlot=-1; return true;}
        return false;
    }

    void renderTransientMessage(){
        if(lastMessage.empty()) return;
        double elapsed = duration_cast<duration<double>>(steady_clock::now()-lastMsgTime).count();
        if(elapsed>MESSAGE_DISPLAY_SEC){ lastMessage.clear(); return; }
        int bw=520,bh=56;
        int bx=(WINDOW_W-bw)/2;
        int by=WINDOW_H-bh-18;
        drawRect(bx,by,bw,bh,0.98f,0.98f,0.88f);
        drawRectBorder(bx,by,bw,bh);
        drawStringAt(lastMessage,bx+12,by+36,GLUT_BITMAP_HELVETICA_12);
    }

    // ---------------- Drawing Helpers ----------------
    void drawRect(int rx,int ry,int rw,int rh,float r,float g,float b){
        glDisable(GL_TEXTURE_2D);
        glColor3f(r,g,b);
        glBegin(GL_QUADS);
            glVertex2i(rx,ry); glVertex2i(rx+rw,ry); glVertex2i(rx+rw,ry+rh); glVertex2i(rx,ry+rh);
        glEnd();
        glColor3f(1,1,1);
    }

    void drawRectBorder(int rx,int ry,int rw,int rh,float lineWidth=2.0f,float r=0.12f,float g=0.12f,float b=0.12f){
        glDisable(GL_TEXTURE_2D);
        glColor3f(r,g,b);
        glLineWidth(lineWidth);
        glBegin(GL_LINE_LOOP);
            glVertex2i(rx,ry); glVertex2i(rx+rw,ry); glVertex2i(rx+rw,ry+rh); glVertex2i(rx,ry+rh);
        glEnd();
        glColor3f(1,1,1);
    }

    void drawTexturedRect(GLuint tex,int texW,int texH,int rx,int ry,int rw,int rh,bool flipX,bool flipY){
        if(!tex) return;
        float tAspect=(texH==0)?1.0f:(float)texW/(float)texH;
        float bAspect=(float)rw/(float)rh;
        int drawW=rw, drawH=rh;
        if(tAspect>bAspect) drawH=(int)(rw/tAspect);
        else drawW=(int)(rh*tAspect);
        int dx=rx+(rw-drawW)/2, dy=ry+(rh-drawH)/2;

        float leftTex = flipX?1.0f:0.0f;
        float rightTex= flipX?0.0f:1.0f;
        float topTex = flipY?0.0f:1.0f;
        float bottomTex=flipY?1.0f:0.0f;

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D,tex);
        glColor3f(1,1,1);
        glBegin(GL_QUADS);
            glTexCoord2f(leftTex, topTex); glVertex2i(dx,dy);
            glTexCoord2f(rightTex, topTex); glVertex2i(dx+drawW,dy);
            glTexCoord2f(rightTex,bottomTex); glVertex2i(dx+drawW,dy+drawH);
            glTexCoord2f(leftTex,bottomTex); glVertex2i(dx,dy+drawH);
        glEnd();
        glBindTexture(GL_TEXTURE_2D,0);
        glDisable(GL_TEXTURE_2D);
    }

    int getBitmapTextWidth(const std::string &s, void* font){
        int w=0; for(unsigned char c:s) w+=glutBitmapWidth(font,c); return w;
    }

    void drawStringAt(const std::string &s,int x,int y,void* font){
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.08f,0.08f,0.08f);
        glRasterPos2i(x,y);
        for(unsigned char c:s) glutBitmapCharacter(font,c);
        glColor3f(1,1,1);
    }

}; // ParkingManager

// ---------------- Global ----------------
std::unique_ptr<ParkingManager> manager;
std::map<std::string, TextureInfo> textures;

void mapMouseToLogical(int x,int y,int &outX,int &outY){
    int winW=glutGet(GLUT_WINDOW_WIDTH);
    int winH=glutGet(GLUT_WINDOW_HEIGHT);
    if(winW<=0) winW=WINDOW_W; if(winH<=0) winH=WINDOW_H;
    float fx=(float)x/(float)winW;
    float fy=(float)y/(float)winH;
    outX=(int)std::round(fx*(WINDOW_W-1));
    outY=(int)std::round(fy*(WINDOW_H-1));
}

// ---------- Load texture ----------
TextureInfo loadTextureFromFile(const std::string &filename){
    stbi_set_flip_vertically_on_load(true);
    int w,h,ch;
    unsigned char *data=stbi_load(filename.c_str(),&w,&h,&ch,4);
    TextureInfo ti;
    if(!data){ std::cerr<<"Failed to load "<<filename<<std::endl; return ti;}
    GLuint tex; glGenTextures(1,&tex);
    glBindTexture(GL_TEXTURE_2D,tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    glBindTexture(GL_TEXTURE_2D,0);
    stbi_image_free(data);
    ti.id=tex; ti.w=w; ti.h=h;
    return ti;
}

// ---------------- GLUT callbacks ----------------
void display(){
    glClearColor(0.97f,0.97f,0.99f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    manager->render();
    glutSwapBuffers();
}

void timerFunc(int){ 
    manager->update();
    glutPostRedisplay();
    glutTimerFunc(1000/30,timerFunc,0);
}

void mouseHandler(int button,int state,int x,int y){
    int mx,my; mapMouseToLogical(x,y,mx,my);
    manager->onMouseClick(mx,my,button,state);
    glutPostRedisplay();
}

void passiveMotionHandler(int x,int y){
    int mx,my; mapMouseToLogical(x,y,mx,my);
    manager->onMouseMove(mx,my);
    glutPostRedisplay();
}

void reshape(int w,int h){
    glViewport(0,0,w,h);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0,WINDOW_W,WINDOW_H,0,-1,1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
}

void keyboard(unsigned char key,int,int){ if(key==27) exit(0); }

// ---------------- main ----------------
int main(int argc,char** argv){
    glutInit(&argc,argv);
    glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGBA);
    glutInitWindowSize(WINDOW_W,WINDOW_H);
    glutInitWindowPosition(100,100);
    glutCreateWindow("Parking Management System - Updated");

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

    textures["car"] = loadTextureFromFile("car.png");
    textures["bike"] = loadTextureFromFile("bike.png");
    textures["truck"] = loadTextureFromFile("truck.png");

    manager = std::make_unique<ParkingManager>();
    manager->setTextures(textures);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutMouseFunc(mouseHandler);
    glutPassiveMotionFunc(passiveMotionHandler);
    glutKeyboardFunc(keyboard);
    glutTimerFunc(0,timerFunc,0);

    std::cout<<"Left-click empty slot -> choose vehicle.\n";
    std::cout<<"Left-click occupied slot -> removal confirmation.\n";
    std::cout<<"First 1 min: 100 Tk, after 60s +1 Tk/sec.\n";
    std::cout<<"ESC to quit.\n";

    glutMainLoop();
    return 0;
}

