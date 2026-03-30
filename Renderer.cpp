// Renderer.cpp — Faz 1 güncelleme: LITHOLOGY renk modu kaldırıldı.
// Diğer tüm render mantığı değişmedi.

#include "Renderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <algorithm>
#include <cmath>

static const char* BV=R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNormal; layout(location=2) in vec3 aColor;
uniform mat4 uModel,uView,uProj; out vec3 vNormal,vColor,vFragPos;
void main(){vec4 wp=uModel*vec4(aPos,1);vFragPos=wp.xyz;vNormal=mat3(transpose(inverse(uModel)))*aNormal;vColor=aColor;gl_Position=uProj*uView*wp;})GLSL";

static const char* IV=R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNormal;
layout(location=2) in vec3 aOffset; layout(location=3) in vec3 aColor;
uniform mat4 uView,uProj; uniform float uBlockSize; out vec3 vNormal,vColor,vFragPos;
void main(){vec3 wp=(aPos*uBlockSize*0.92)+aOffset;vFragPos=wp;vNormal=aNormal;vColor=aColor;gl_Position=uProj*uView*vec4(wp,1);})GLSL";

static const char* LF=R"GLSL(
#version 330 core
in vec3 vNormal,vColor,vFragPos; uniform vec3 uLightDir,uCamPos; uniform float uAmbient,uDiffuse; out vec4 FragColor;
void main(){vec3 N=normalize(vNormal),L=normalize(uLightDir),V=normalize(uCamPos-vFragPos),H=normalize(L+V);
float diff=max(dot(N,L),0),spec=pow(max(dot(N,H),0),16)*0.05,fill=max(dot(N,-L),0)*0.15;
float fog=1-clamp((length(uCamPos-vFragPos)-800)/2000,0,0.8);
vec3 lit=vColor*uAmbient+vColor*diff*uDiffuse+vColor*fill+vec3(spec);
FragColor=vec4(mix(vec3(0.04,0.06,0.10),lit,fog),1);})GLSL";

static const char* FV=R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos; layout(location=2) in vec3 aColor;
uniform mat4 uModel,uView,uProj; out vec3 vColor;
void main(){vColor=aColor;gl_Position=uProj*uView*uModel*vec4(aPos,1);})GLSL";

static const char* FF=R"GLSL(
#version 330 core
in vec3 vColor; out vec4 FragColor; void main(){FragColor=vec4(vColor,1);})GLSL";

static GLuint cs(GLenum t,const char* src){
    GLuint s=glCreateShader(t);glShaderSource(s,1,&src,nullptr);glCompileShader(s);
    GLint ok;char log[1024];glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){glGetShaderInfoLog(s,1024,nullptr,log);std::cerr<<"[Shader] "<<log;}
    return s;
}
static GLuint lp(const char* v,const char* f){
    GLuint p=glCreateProgram();
    glAttachShader(p,cs(GL_VERTEX_SHADER,v));
    glAttachShader(p,cs(GL_FRAGMENT_SHADER,f));
    glLinkProgram(p);return p;
}
struct IS{
    GLuint id=0;
    IS(const char*v,const char*f){id=lp(v,f);}
    void use()const{glUseProgram(id);}
    void m4(const char*n,const glm::mat4&v)const{glUniformMatrix4fv(glGetUniformLocation(id,n),1,0,&v[0][0]);}
    void v3(const char*n,const glm::vec3&v)const{glUniform3fv(glGetUniformLocation(id,n),1,&v[0]);}
    void f1(const char*n,float v)const{glUniform1f(glGetUniformLocation(id,n),v);}
};

Renderer::Renderer():_meshModel(std::make_unique<MeshModel>()){}
Renderer::~Renderer(){}

bool Renderer::init(){
    _bShader=new IS(BV,LF);
    _iShader=new IS(IV,LF);
    _gShader=new IS(FV,FF);
    buildGrid(2000.f,100);
    buildAxes(200.f);
    buildInstancedCube();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    return true;
}

glm::vec3 Renderer::gradeToColor(float g){
    float t=std::clamp(g/3.f,0.f,1.f);
    if(t<.5f) return glm::mix(glm::vec3(.08f,.18f,.90f),glm::vec3(.95f,.88f,.10f),t*2);
    return glm::mix(glm::vec3(.95f,.88f,.10f),glm::vec3(.90f,.10f,.10f),(t-.5f)*2);
}

void Renderer::loadMesh(const PitMeshData& d){ _meshModel->upload(d); }

void Renderer::loadBlockModel(const BlockModel& model, BlockColorMode mode){
    _blockSize = model.blockSize();
    struct ID{ glm::vec3 o, c; };
    std::vector<ID> inst;
    inst.reserve(model.getBlocks().size());

    for(const auto& b : model.getBlocks()){
        if(b.state == LGState::DISCARDED || b.state == LGState::MINED) continue;
        glm::vec3 col;
        switch(mode){
            case BlockColorMode::FLAT:
                col = glm::vec3(.50f,.54f,.60f);
                break;
            case BlockColorMode::GRADE:
                col = gradeToColor(b.grade);
                break;
            case BlockColorMode::ECONOMIC:
                col = b.value > 0 ? glm::vec3(.15f,.78f,.28f)
                                  : glm::vec3(.78f,.18f,.15f);
                break;
            // LITHOLOGY case kaldırıldı — Faz 1
        }
        inst.push_back({b.worldPos, col});
    }
    _instanceCount = (int)inst.size();
    glBindVertexArray(_cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, _instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, inst.size()*sizeof(ID), inst.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(2,3,GL_FLOAT,0,sizeof(ID),(void*)0);
    glEnableVertexAttribArray(2); glVertexAttribDivisor(2,1);
    glVertexAttribPointer(3,3,GL_FLOAT,0,sizeof(ID),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(3); glVertexAttribDivisor(3,1);
    glBindVertexArray(0);
    std::cout<<"[Renderer] Instances: "<<_instanceCount<<"\n";
}

void Renderer::loadDrillholes(const DrillholeDatabase& db){
    auto paths=db.getDesurveyedPaths();
    std::vector<float> v; _drillVertCount=0;
    for(const auto& p:paths){
        if(p.points.size()<2) continue;
        glm::vec3 col=gradeToColor(p.maxGrade);
        for(size_t i=0;i+1<p.points.size();++i){
            const auto& p0=p.points[i], &p1=p.points[i+1];
            v.insert(v.end(),{p0.x,p0.y,p0.z,col.r,col.g,col.b});
            v.insert(v.end(),{p1.x,p1.y,p1.z,col.r,col.g,col.b});
            _drillVertCount+=2;
        }
    }
    if(!_drillVAO){ glGenVertexArrays(1,&_drillVAO); glGenBuffers(1,&_drillVBO); }
    glBindVertexArray(_drillVAO);
    glBindBuffer(GL_ARRAY_BUFFER,_drillVBO);
    glBufferData(GL_ARRAY_BUFFER,v.size()*sizeof(float),v.data(),GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,0,6*sizeof(float),(void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(2,3,GL_FLOAT,0,6*sizeof(float),(void*)(3*sizeof(float))); glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    std::cout<<"[Renderer] Drill verts: "<<_drillVertCount<<"\n";
}

void Renderer::render(const Camera& cam,float aspect,const RenderSettings& s){
    glClearColor(.04f,.06f,.10f,1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glm::mat4 view=cam.viewMatrix(),proj=cam.projMatrix(aspect),model=glm::mat4(1);

    {auto*bs=static_cast<IS*>(_bShader);bs->use();
     bs->m4("uModel",model);bs->m4("uView",view);bs->m4("uProj",proj);
     bs->v3("uLightDir",_lightDir);bs->v3("uCamPos",cam.position());
     bs->f1("uAmbient",s.ambientStr);bs->f1("uDiffuse",s.diffuseStr);
     if(s.showWireframe){
         glPolygonOffset(-1,-1);glEnable(GL_POLYGON_OFFSET_LINE);
         glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
         _meshModel->drawWireframe();
         glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glDisable(GL_POLYGON_OFFSET_LINE);
     } else if(!s.showBlocks) _meshModel->draw();
    }

    if(s.showBlocks && _instanceCount>0){
        glDisable(GL_CULL_FACE);
        auto*is=static_cast<IS*>(_iShader);is->use();
        is->m4("uView",view);is->m4("uProj",proj);
        is->v3("uLightDir",_lightDir);is->v3("uCamPos",cam.position());
        is->f1("uAmbient",s.ambientStr);is->f1("uDiffuse",s.diffuseStr);
        is->f1("uBlockSize",_blockSize);
        glBindVertexArray(_cubeVAO);
        glDrawArraysInstanced(GL_TRIANGLES,0,36,_instanceCount);
        glBindVertexArray(0);glEnable(GL_CULL_FACE);
    }

    if(s.showDrillholes && _drillVertCount>0){
        auto*gs=static_cast<IS*>(_gShader);gs->use();
        gs->m4("uModel",model);gs->m4("uView",view);gs->m4("uProj",proj);
        glLineWidth(2.5f);glBindVertexArray(_drillVAO);
        glDrawArrays(GL_LINES,0,_drillVertCount);
        glBindVertexArray(0);glLineWidth(1);
    }

    if(s.showGrid){
        auto*gs=static_cast<IS*>(_gShader);gs->use();
        gs->m4("uModel",model);gs->m4("uView",view);gs->m4("uProj",proj);
        glBindVertexArray(_gridVAO);glDrawArrays(GL_LINES,0,_gridVertCount);
    }

    if(s.showAxes){
        auto*gs=static_cast<IS*>(_gShader);gs->use();
        gs->m4("uModel",model);gs->m4("uView",view);gs->m4("uProj",proj);
        glLineWidth(2.5f);glBindVertexArray(_axisVAO);
        glDrawArrays(GL_LINES,0,_axisVertCount);glLineWidth(1);
    }
}

void Renderer::buildInstancedCube(){
    float v[]={
        -0.5f,-0.5f,-0.5f,0,0,-1, 0.5f,0.5f,-0.5f,0,0,-1, 0.5f,-0.5f,-0.5f,0,0,-1,
         0.5f,0.5f,-0.5f,0,0,-1,-0.5f,-0.5f,-0.5f,0,0,-1,-0.5f,0.5f,-0.5f,0,0,-1,
        -0.5f,-0.5f,0.5f,0,0,1,  0.5f,-0.5f,0.5f,0,0,1,   0.5f,0.5f,0.5f,0,0,1,
         0.5f,0.5f,0.5f,0,0,1,  -0.5f,0.5f,0.5f,0,0,1,   -0.5f,-0.5f,0.5f,0,0,1,
        -0.5f,0.5f,0.5f,-1,0,0, -0.5f,0.5f,-0.5f,-1,0,0, -0.5f,-0.5f,-0.5f,-1,0,0,
        -0.5f,-0.5f,-0.5f,-1,0,0,-0.5f,-0.5f,0.5f,-1,0,0,-0.5f,0.5f,0.5f,-1,0,0,
         0.5f,0.5f,0.5f,1,0,0,   0.5f,-0.5f,-0.5f,1,0,0,  0.5f,0.5f,-0.5f,1,0,0,
         0.5f,-0.5f,-0.5f,1,0,0, 0.5f,0.5f,0.5f,1,0,0,    0.5f,-0.5f,0.5f,1,0,0,
        -0.5f,-0.5f,-0.5f,0,-1,0,0.5f,-0.5f,-0.5f,0,-1,0, 0.5f,-0.5f,0.5f,0,-1,0,
         0.5f,-0.5f,0.5f,0,-1,0,-0.5f,-0.5f,0.5f,0,-1,0, -0.5f,-0.5f,-0.5f,0,-1,0,
        -0.5f,0.5f,-0.5f,0,1,0,  0.5f,0.5f,0.5f,0,1,0,    0.5f,0.5f,-0.5f,0,1,0,
         0.5f,0.5f,0.5f,0,1,0,  -0.5f,0.5f,-0.5f,0,1,0,  -0.5f,0.5f,0.5f,0,1,0};
    glGenVertexArrays(1,&_cubeVAO);glGenBuffers(1,&_cubeVBO);glGenBuffers(1,&_instanceVBO);
    glBindVertexArray(_cubeVAO);glBindBuffer(GL_ARRAY_BUFFER,_cubeVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(v),v,GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,0,6*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,0,6*sizeof(float),(void*)(3*sizeof(float)));glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void Renderer::buildGrid(float sz,int d){
    std::vector<float> v;float st=sz/d,h=sz/2,gc=.28f;
    for(int i=0;i<=d;++i){float p=-h+i*st;
        v.insert(v.end(),{p,0,-h,gc,gc,gc,p,0,h,gc,gc,gc});
        v.insert(v.end(),{-h,0,p,gc,gc,gc,h,0,p,gc,gc,gc});}
    glGenVertexArrays(1,&_gridVAO);glGenBuffers(1,&_gridVBO);
    glBindVertexArray(_gridVAO);glBindBuffer(GL_ARRAY_BUFFER,_gridVBO);
    glBufferData(GL_ARRAY_BUFFER,v.size()*sizeof(float),v.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,0,6*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
    glVertexAttribPointer(2,3,GL_FLOAT,0,6*sizeof(float),(void*)(3*sizeof(float)));glEnableVertexAttribArray(2);
    _gridVertCount=(int)v.size()/6;
}

void Renderer::buildAxes(float len){
    float v[]={0,0,0,1,0,0,len,0,0,1,0,0, 0,0,0,0,1,0,0,len,0,0,1,0, 0,0,0,0,.5f,1,0,0,len,0,.5f,1};
    glGenVertexArrays(1,&_axisVAO);glGenBuffers(1,&_axisVBO);
    glBindVertexArray(_axisVAO);glBindBuffer(GL_ARRAY_BUFFER,_axisVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(v),v,GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,0,6*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
    glVertexAttribPointer(2,3,GL_FLOAT,0,6*sizeof(float),(void*)(3*sizeof(float)));glEnableVertexAttribArray(2);
    _axisVertCount=6;
}

void Renderer::setWindowSize(int w,int h){
    _width=w; _height=h; glViewport(0,0,w,h);
}
