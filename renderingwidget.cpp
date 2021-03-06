// (c) 2015 David Vyvlečka, AGPLv3

#include <QtWidgets>
#include <QtOpenGL>
#include <QMessageBox>
#include "renderingwidget.h"

RenderingWidget::RenderingWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{    
    Axes = true;
    Grid = false;
    Info = true;
    xPos = 1.0f;
    yPos = 0.5f;
    zPos = 1.0f;
    angleX = 0.0f;
    angleY = 70.0f;
    zoom = 100.0f;
    model.setToIdentity();
    model.rotate(90, -1.0f,0.0f,0.0f);  //Rotate to OpenGL axes system
    smallAxesBox = QVector4D(5, 5, 105, 105);
    gridStep = 1;
    shiftPressed = false;
    minDiam = 1.0f;
    background_col = Qt::black;
    text_col = Qt::white;
    mouseInverted = false;
    w = DEFAULT_RES_X;
    h = DEFAULT_RES_Y;
}


RenderingWidget::~RenderingWidget()
{
    glDeleteBuffers(1, &axes_vbo);
    glDeleteBuffers(1, &grid_vbo);
}

void RenderingWidget::writeSettings()
{
    QSettings settings;
    settings.setValue("axes", Axes);
    settings.setValue("grid", Grid);
    settings.setValue("info", Info);
}

void RenderingWidget::invertMouse()
{
    mouseInverted = !mouseInverted;
}

void RenderingWidget::setController(admeshController* cnt)
{
    controller = cnt;
}

QSize RenderingWidget::minimumSizeHint() const
{
    return QSize(50, 50);
}

QSize RenderingWidget::sizeHint() const
{
    QSettings settings;
    return QSize(settings.value("width",DEFAULT_RES_X).toInt(), settings.value("height",DEFAULT_RES_Y).toInt());
}

void RenderingWidget::setBackground(QColor b)
{
    background_col = b;
}

void RenderingWidget::setTextCol(QColor text)
{
    text_col = text;
}

void RenderingWidget::setFrontView()
{
    angleX = 0;
    angleY = 90;
    reDraw();
}

void RenderingWidget::setBackView()
{
    angleX = 180;
    angleY = 90;
    reDraw();
}

void RenderingWidget::setLeftView()
{
    angleX = 270;
    angleY = 90;
    reDraw();
}

void RenderingWidget::setRightView()
{
    angleX = 90;
    angleY = 90;
    reDraw();
}

void RenderingWidget::setTopView()
{
    angleX = 0;
    angleY = 0;
    reDraw();
}

void RenderingWidget::setBottomView()
{
    angleX = 0;
    angleY = 180;
    reDraw();
}

void RenderingWidget::toggleGrid()
{
    Grid = !Grid;
    update();
}

void RenderingWidget::toggleAxes()
{
    Axes = !Axes;
    update();
}

void RenderingWidget::toggleInfo()
{
    Info = !Info;
    update();
}

void RenderingWidget::initializeGL()
{
    initializeGLFunctions();
    initShaders();
    glGenBuffers(1, &axes_vbo);
    glGenBuffers(1, &grid_vbo);
    vao.create();
    selection = false;
    initAxes();
    initGrid();    
    glClearColor(background_col.redF(),background_col.greenF(),background_col.blueF(),1.0);
    glEnable(GL_DEPTH_TEST);
    pickFboFormat.setAttachment(QOpenGLFramebufferObject::Depth);
    pickFboFormat.setTextureTarget(GL_TEXTURE_2D);
    pickFboFormat.setInternalTextureFormat(GL_RGBA8);    
    recalculateGridStep();
    reDraw();
}

void RenderingWidget::initShaders(){
    if (!program.addShaderFromSourceFile(QGLShader::Vertex, ":/vshader.glsl")) close();

    if (!program.addShaderFromSourceFile(QGLShader::Fragment, ":/fshader.glsl")) close();

    if (!program.link()) close();

    if (!program.bind()) close();

    if (!pick_program.addShaderFromSourceFile(QGLShader::Vertex, ":/picking_vshader.glsl")) close();

    if (!pick_program.addShaderFromSourceFile(QGLShader::Fragment, ":/picking_fshader.glsl")) close();

    if (!pick_program.link()) close();

    if (!pick_program.bind()) close();

}

void RenderingWidget::timerEvent(QTimerEvent *)
{
    update();
}

void RenderingWidget::paintGL()
{
    QPainter painter;
    painter.begin(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.beginNativePainting();      //Start rendering 3D content

    glClearColor(background_col.redF(),background_col.greenF(),background_col.blueF(),1.0);      //Set OpenGl states
    glEnable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    vao.bind();
    program.bind();                     //Use shader program
    glViewport(0, 0, w, h);
    getCamPos();

    program.setUniformValue("differ_hue", false);
    program.setUniformValue("mvp_matrix", projection * view * model);   //Draw main window contents

    if(Axes) drawAxes();
    if(Grid) drawGrid();

    program.setUniformValue("mvp_matrix", projection * view * model);
    controller->drawAll(&program);

    glViewport(smallAxesBox.x(), smallAxesBox.y(), smallAxesBox.z(), smallAxesBox.w()); // xStart, yStart, xWidth, yWidth
    program.setUniformValue("mvp_matrix", orthographic * smallView * model); //Draw corner orthographic axes
    drawSmallAxes();
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glDisable(GL_DEPTH_TEST);
    program.release();
    vao.release();
    painter.endNativePainting();        //Start rendering 2D content

    painter.setRenderHint(QPainter::TextAntialiasing);
    if(Info) drawInfo(&painter);
    drawLabels(&painter);

    if(selection){                      //Handle picking
        painter.beginNativePainting();
        vao.bind();
        doPicking();
        selection = false;
        vao.release();
    }
    painter.end();
}

void RenderingWidget::recalculateProjectionNear()
{
    if(2*minDiam < zoom){
        projection.setToIdentity();
        projection.perspective(PERSPECTIVE, (GLfloat)width()/(GLfloat)height(), 1.0, MAX_VIEW_DISTANCE);
    }else{
        projection.setToIdentity();
        projection.perspective(PERSPECTIVE, (GLfloat)width()/(GLfloat)height(), MIN_VIEW_DISTANCE, MAX_VIEW_DISTANCE);
    }
}

void RenderingWidget::resizeGL(int width, int height)
{
    w = width * this->devicePixelRatio();
    h = height * this->devicePixelRatio();
    glViewport(0, 0, w, h);
    projection.setToIdentity();
    projection.perspective(PERSPECTIVE, (GLfloat)w/(GLfloat)h, MIN_VIEW_DISTANCE, MAX_VIEW_DISTANCE);
    orthographic.setToIdentity();
    orthographic.ortho (-1.0f,1.0f,-1.0f,1.0f, -100, 100 );
}

void RenderingWidget::doPicking(){
    glViewport(0, 0, w, h);
    QOpenGLFramebufferObject fbo(w,h, pickFboFormat);
    fbo.bind();
    glEnable(GL_DEPTH_TEST);
    glClearColor(1.0,1.0,1.0,1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);    
    pick_program.bind();
    pick_program.setUniformValue("mvp_matrix", projection * view * model);
    controller->drawPicking(&pick_program);
    QImage img = fbo.toImage();
    QRgb color = img.pixel(lastSelectionPos.x(),lastSelectionPos.y());
    int id = qBlue(color) + qGreen(color)*255 + qRed(color)*255*255;
    if(shiftPressed){
        controller->setActiveByIndex(id);
    }else{
        controller->setAllInactive();
        controller->setActiveByIndex(id);
    }
    pick_program.release();
    fbo.release();
}

void RenderingWidget::drawInfo(QPainter *painter)
{
    glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

    QString style;
    style = "<style type=\"text/css\">";
    if(text_col == Qt::white) style += "*{color:white;}";
    else if(text_col == Qt::black) style += "*{color:black;}";
    style += "</style>";

    QString text = style + controller->getInfo();
#ifdef QT_DEBUG
    QTextStream(&text) << "<tr><td width=\"60%\" class=\"desc\">"<<_("Camera angle X:") <<"</td><td width=\"40%\">"<<angleX<<"</td></tr>" <<
                          "<tr><td width=\"60%\" class=\"desc\">"<<_("Camera angle Y:") <<"</td><td width=\"40%\">"<<angleY<<"</td></tr>";
#endif
    if(Grid) QTextStream(&text) << "<tr><td width=\"60%\" class=\"desc\">"<<_("Grid step:") <<"</td><td width=\"40%\">"<<gridStep<<"</td></tr></table>";
    else QTextStream(&text) << "</table>";
    QTextDocument* doc = new QTextDocument(this);
    doc->setUndoRedoEnabled(false);
    doc->setPageSize(QSizeF(qMin((int)(width()*0.7),300), height()));
    doc->setHtml(text);
    doc->setUseDesignMetrics(true);
    doc->setDefaultTextOption(QTextOption(Qt::AlignLeft));
    doc->drawContents(painter);
    delete doc;
}

QVector2D RenderingWidget::getScreenCoords(QVector3D worldCoords){
    QVector4D homogCoords = orthographic * smallView * model * QVector4D(worldCoords, 1.0);
    GLfloat X = homogCoords.x() / homogCoords.w();
    GLfloat Y = homogCoords.y() / homogCoords.w();
    return QVector2D(smallAxesBox.x() + smallAxesBox.z() * (X+1)/2,smallAxesBox.y() + smallAxesBox.w() * (Y+1)/2);
}

void RenderingWidget::drawLabels(QPainter *painter)
{
    glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
    QVector2D screenCoords = getScreenCoords(QVector3D(0.7, -0.5 , -0.55));  // X axis
    painter->setPen(Qt::red);
    painter->drawText(screenCoords.x(),height()-screenCoords.y(),"x");
    screenCoords = getScreenCoords(QVector3D(-0.5, 0.7, -0.55));           // Y axis
    painter->setPen(Qt::green);
    painter->drawText(screenCoords.x(),height()-screenCoords.y(),"y");
    screenCoords = getScreenCoords(QVector3D(-0.5, -0.5, 0.7));              // Z axis
    painter->setPen(Qt::blue);
    painter->drawText(screenCoords.x(),height()-screenCoords.y(),"z");
}

void RenderingWidget::getCamPos()
{
    xPos = sin(angleY*(M_PI/180)) * sin(angleX*(M_PI/180));
    yPos = cos(angleY*(M_PI/180));
    zPos = sin(angleY*(M_PI/180)) * cos(angleX*(M_PI/180));

    GLfloat dt=1.0f; //Small difference to get second point

    GLfloat upX=sin(angleY*(M_PI/180)-dt) * sin(angleX*(M_PI/180)) -xPos;
    GLfloat upY=cos(angleY*(M_PI/180)-dt) -yPos;
    GLfloat upZ=sin(angleY*(M_PI/180)-dt) * cos(angleX*(M_PI/180)) -zPos;

    view.setToIdentity();
    view.translate(xTrans, yTrans, -zoom);
    view.lookAt (QVector3D(xPos, yPos,zPos), QVector3D(0.0, 0.0, 0.0), QVector3D(upX, upY, upZ));

    smallView.setToIdentity();
    smallView.lookAt (QVector3D(xPos, yPos, zPos), QVector3D(0.0, 0.0, 0.0), QVector3D(upX, upY, upZ));
}

void RenderingWidget::normalizeAngles()
{
    if(angleX > 360.0f) angleX = fmod((double)angleX,360.0);
    if(angleY > 360.0f) angleY = fmod((double)angleY,360.0);
    if(angleX < 0.0f) angleX = 360.0f - angleX;
    if(angleY < 0.0f) angleY = 360.0f - angleY;
}

void RenderingWidget::recalculateGridStep()
{
    int factor = (int)(zoom/GRID_SIZE);
    if(factor > 5){
        int remainder = factor % 5;
        factor -= remainder;
    }else{
        factor = qMax(1,factor);
    }
    if(factor != gridStep) {
        gridStep = factor;
        initGrid();
    }
}

void RenderingWidget::toggleShift()
{
    shiftPressed = !shiftPressed;
}

void RenderingWidget::wheelEvent(QWheelEvent* event)
{
    float tmp = zoom;
    float factor;
    if(this->devicePixelRatio()>1) factor=1.1;
    else factor = 1.25;
    if(event->delta()<0){
        tmp *= factor;
    }else{
        tmp *= 1/factor;
    }
    if(tmp > MIN_ZOOM && tmp < MAX_ZOOM){
        zoom = tmp;
        recalculateGridStep();
        recalculateProjectionNear();
    }
    reDraw();
}

void RenderingWidget::mouseReleaseEvent(QMouseEvent *event)
{
    timer.stop();
    event->accept();
}

void RenderingWidget::mousePressEvent(QMouseEvent *event)
{
    timer.start(33, this);
    if(event->buttons() & Qt::LeftButton && !shiftPressed) lastPos = event->pos();
    if(event->buttons() & Qt::RightButton) {
        lastSelectionPos = event->pos();
        selection = true;
    }
    if((event->buttons() & Qt::MiddleButton) || (event->buttons() & Qt::LeftButton && shiftPressed)) lastTransPos = event->pos();
}

void RenderingWidget::mouseMoveEvent(QMouseEvent *event)
{
    if(event->buttons() & Qt::LeftButton && !shiftPressed)
    {
        int dx = event->x() - lastPos.x();
        int dy = event->y() - lastPos.y();

        if(!mouseInverted){
            angleY -= dy;
            if(angleY>180)angleX += dx; //take care of opposite rotation upside down
            else angleX -=dx;
        }else{
            angleY += dy;
            if(angleY>180)angleX -= dx; //take care of opposite rotation upside down
            else angleX +=dx;
        }

        normalizeAngles();
        lastPos = event->pos();
    }
    if((event->buttons() & Qt::MiddleButton) || (event->buttons() & Qt::LeftButton && shiftPressed))
    {
        int dx = (event->x() - lastTransPos.x());
        int dy = (event->y() - lastTransPos.y());

        if(!mouseInverted){
            xTrans += (GLfloat)dx/3;
            yTrans -= (GLfloat)dy/3;
        }else{
            xTrans -= (GLfloat)dx/3;
            yTrans += (GLfloat)dy/3;
        }
        lastTransPos = event->pos();
    }
    //reDraw();
}

void RenderingWidget::initAxes(){
    GLfloat val = 0.5;
    if(this->devicePixelRatio()>1) val = 1.5f;
    GLfloat vertices[]={
       AXIS_SIZE, 0.0 , 0.0,    //Main axes
       1.0, 1.0, 1.0,
       0.0, 0.0, 0.0,
       1.0, 1.0, 1.0,
        0.0, 0.0 , 0.0,
        1.0, 1.0, 1.0,
        -AXIS_SIZE, 0.0, 0.0,
        1.0, 1.0, 1.0,
       0.0, AXIS_SIZE, 0.0,
       1.0, 1.0, 1.0,
       0.0, 0.0, 0.0,
       1.0, 1.0, 1.0,
        0.0, 0.0, 0.0,
        1.0, 1.0, 1.0,
        0.0, -AXIS_SIZE, 0.0,
        1.0, 1.0, 1.0,
       0.0, 0.0, AXIS_SIZE,
       1.0, 1.0, 1.0,
       0.0, 0.0, 0.0,
       1.0, 1.0, 1.0,
        0.0, 0.0, 0.0,
        1.0, 1.0, 1.0,
        0.0, 0.0, -AXIS_SIZE,
        1.0, 1.0, 1.0,           //Small corner axes
       val, -0.5 , -0.5, //x
       1.0, 1.0, 1.0,
       -0.5, -0.5, -0.5,
       1.0, 1.0, 1.0,
       -0.5, -0.5, -0.5, //y
       1.0, 1.0, 1.0,
       -0.5, val, -0.5,
       1.0, 1.0, 1.0,
       -0.5, -0.5, val, //z
       1.0, 1.0, 1.0,
       -0.5, -0.5, -0.5,
       1.0, 1.0, 1.0,

    };
    glBindBuffer(GL_ARRAY_BUFFER, axes_vbo);
    glBufferData(GL_ARRAY_BUFFER, 108 * sizeof(GLfloat), vertices, GL_STATIC_DRAW);
}

void RenderingWidget::initGrid(){
    int size= (GRID_SIZE+1)* 4 * 4; // 4*4 for 4 sides of vertexes * 4 float
    GLfloat *vertices= new GLfloat[size];
    for(int i = 0;i<= GRID_SIZE*2; i++){
        vertices[i*4]=-GRID_SIZE*gridStep;
        vertices[i*4 + 1]=(i-GRID_SIZE)*gridStep;
        vertices[i*4 + 2]=GRID_SIZE*gridStep;
        vertices[i*4 + 3]=(i-GRID_SIZE)*gridStep;
    }
    int ind = GRID_SIZE*2*4+4;
    for(int i = 0;i<= GRID_SIZE*2; i++){
        vertices[ind + (i*4)]=(i-GRID_SIZE)*gridStep;
        vertices[ind + (i*4+1)]=-GRID_SIZE*gridStep;
        vertices[ind + (i*4+2)]=(i-GRID_SIZE)*gridStep;
        vertices[ind + (i*4+3)]=GRID_SIZE*gridStep;
    }
    glBindBuffer(GL_ARRAY_BUFFER, grid_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GRID_SIZE+1) * 4 * 4 * sizeof(GLfloat), vertices, GL_DYNAMIC_DRAW);
    delete []vertices;
}

void RenderingWidget::drawAxes()
{
    glBindBuffer(GL_ARRAY_BUFFER, axes_vbo);

    int vertexLocation = program.attributeLocation("a_position");
    program.enableAttributeArray(vertexLocation);
    glVertexAttribPointer(vertexLocation, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat)*6, 0);

    int normalLocation = program.attributeLocation("a_normal");
    program.enableAttributeArray(normalLocation);
    glVertexAttribPointer(normalLocation, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat)*6, (const void *)(sizeof(GLfloat)*3));

    program.setUniformValue("color", RED);

    glDrawArrays(GL_LINES, 0, 4);
    program.setUniformValue("color", GREEN);
    glDrawArrays(GL_LINES, 4, 4);
    program.setUniformValue("color", BLUE);
    glDrawArrays(GL_LINES, 8, 4);
}

void RenderingWidget::drawSmallAxes()
{
    glBindBuffer(GL_ARRAY_BUFFER, axes_vbo);
    int vertexLocation = program.attributeLocation("a_position");
    program.enableAttributeArray(vertexLocation);
    glVertexAttribPointer(vertexLocation, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat)*6, 0);

    int normalLocation = program.attributeLocation("a_normal");
    program.enableAttributeArray(normalLocation);
    glVertexAttribPointer(normalLocation, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat)*6, (const void *)(sizeof(GLfloat)*3));

    program.setUniformValue("color", RED);
    glDrawArrays(GL_LINES, 12, 2);
    program.setUniformValue("color", GREEN);
    glDrawArrays(GL_LINES, 14, 2);
    program.setUniformValue("color", BLUE);
    glDrawArrays(GL_LINES, 16, 2);
}

void RenderingWidget::drawGrid()
{
    glBindBuffer(GL_ARRAY_BUFFER, grid_vbo);
    int vertexLocation = program.attributeLocation("a_position");
    program.enableAttributeArray(vertexLocation);
    glVertexAttribPointer(vertexLocation, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat)*2, 0);

    QColor gridCol;
    if(background_col == Qt::white) gridCol = Qt::gray;
    else gridCol = Qt::white;
    program.setUniformValue("color", QVector3D(gridCol.redF(),gridCol.greenF(),gridCol.blueF()));
    glDrawArrays(GL_LINES, 0, (GRID_SIZE)*8 +4);
}

void RenderingWidget::reDraw()
{
    update();
}

void RenderingWidget::reCalculatePosition()
{
    float val = controller->getMaxDiameter();
    xPos = 1.0f;
    yPos = 0.5f;
    zPos = 1.0f;
    angleX = 0.0f;
    angleY = 70.0f;
    if(val > 0.0) zoom = qMin(float(2.5*val),MAX_ZOOM);
    else zoom = 100;
    if (val>minDiam) minDiam = val;
    recalculateProjectionNear();
    recalculateGridStep();
    reDraw();
}

void RenderingWidget::centerPosition()
{
    xTrans = yTrans = 0;
    reDraw();
}

