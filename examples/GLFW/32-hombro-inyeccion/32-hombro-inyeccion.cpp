//==============================================================================
/*
    SIMULADOR HÁPTICO DE HOMBRO - INYECCIÓN INTRAARTICULAR
    CHAI3D 3.3.0 + Geomagic Touch / Phantom Haptic Device
    
    Proyecto: 32-hombro-inyeccion
    Modelo anatómico: Hombro_Anatomico_Inyeccion.obj
*/
//==============================================================================

#include "chai3d.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

using namespace chai3d;
using namespace std;

//------------------------------------------------------------------------------
// CONSTANTES Y CONFIGURACIÓN GENERAL
//------------------------------------------------------------------------------

// Modo estéreo (desactivado por defecto)
cStereoMode stereoMode = C_STEREO_DISABLED;

// Pantalla completa y visualización en espejo
bool fullscreen = false;
bool mirroredDisplay = false;

// Estado del ratón para control de cámara
enum MouseState
{
    MOUSE_IDLE,
    MOUSE_ROTATE_CAMERA,
    MOUSE_PAN_CAMERA
};
MouseState mouseState = MOUSE_IDLE;
double mouseX = 0.0;
double mouseY = 0.0;

// Radio inicial de la esfera puntero (en metros: 0.006 m = 6 mm de radio, 12 mm de diámetro)
double toolRadius = 0.006;

// Factor de rigidez háptica relativo a la rigidez máxima del dispositivo
double stiffnessFactor = 0.40;

// Visibilidad / transparencia del músculo deltoides (capa superficial)
bool deltoidVisible = true;
float deltoidAlpha = 0.35f;

// Mostrar HUD / información en pantalla
bool showHUD = true;

//------------------------------------------------------------------------------
// VARIABLES GLOBALES DE SIMULACIÓN
//------------------------------------------------------------------------------

// Mundo virtual CHAI3D
cWorld* world = nullptr;

// Cámara y visor
cCamera* camera = nullptr;
cViewport* viewport = nullptr;

// Fuentes de iluminación
cDirectionalLight* lightPrincipal = nullptr;
cDirectionalLight* lightSecundaria = nullptr;

// Modelo anatómico del hombro (MultiMesh)
cMultiMesh* shoulderMesh = nullptr;

// Dispositivo háptico y herramienta (cursor esférico con algoritmo god-object/proxy)
cHapticDeviceHandler* handler = nullptr;
cGenericHapticDevicePtr hapticDevice = nullptr;
cHapticDeviceInfo hapticDeviceInfo;
cToolCursor* tool = nullptr;

// Hilos y sincronización
cThread* hapticsThread = nullptr;
bool simulationRunning = false;
bool simulationFinished = true;

// Medidores de frecuencia
cFrequencyCounter freqCounterGraphics;
cFrequencyCounter freqCounterHaptics;

// Ventana GLFW
GLFWwindow* window = nullptr;
int windowW = 1280;
int windowH = 800;
int framebufferW = 0;
int framebufferH = 0;
int swapInterval = 1;

// Elementos de la interfaz de usuario (HUD)
cFontPtr fontTitle;
cFontPtr fontInfo;
cFontPtr fontHelp;
cLabel* labelTitle = nullptr;
cLabel* labelDevice = nullptr;
cLabel* labelPosition = nullptr;
cLabel* labelForce = nullptr;
cLabel* labelRates = nullptr;
cLabel* labelHelp = nullptr;

// Centro anatómico estimado del modelo (para centrarlo en el origen)
const cVector3d SHOULDER_CENTER(0.0285, -0.1444, 0.0006);

//------------------------------------------------------------------------------
// DECLARACIÓN DE FUNCIONES
//------------------------------------------------------------------------------

void onWindowSizeCallback(GLFWwindow* a_window, int a_width, int a_height);
void onFrameBufferSizeCallback(GLFWwindow* a_window, int a_width, int a_height);
void onWindowContentScaleCallback(GLFWwindow* a_window, float a_xscale, float a_yscale);
void onErrorCallback(int a_error, const char* a_description);
void onKeyCallback(GLFWwindow* a_window, int a_key, int a_scancode, int a_action, int a_mods);
void onMouseButtonCallback(GLFWwindow* a_window, int a_button, int a_action, int a_mods);
void onCursorPosCallback(GLFWwindow* a_window, double a_posX, double a_posY);
void onScrollCallback(GLFWwindow* a_window, double a_offsetX, double a_offsetY);

void setCameraView(int a_viewIndex);
void updateHUD();
void renderGraphics();
void renderHaptics();
void closeApp();

//==============================================================================
// FUNCIÓN PRINCIPAL
//==============================================================================

int main(int argc, char* argv[])
{
    cout << "========================================================================" << endl;
    cout << "  SIMULADOR HÁPTICO DE HOMBRO - INYECCIÓN INTRAARTICULAR               " << endl;
    cout << "  Integración CHAI3D 3.3.0 + Geomagic Touch / OpenHaptics               " << endl;
    cout << "========================================================================" << endl;

    //--------------------------------------------------------------------------
    // 1. INICIALIZACIÓN DE VENTANA GRÁFICA GLFW
    //--------------------------------------------------------------------------
    if (!glfwInit())
    {
        cerr << "Error: No se pudo inicializar GLFW." << endl;
        return -1;
    }

    glfwSetErrorCallback(onErrorCallback);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    window = glfwCreateWindow(windowW, windowH, "Simulador Háptico de Hombro - Geomagic Touch", NULL, NULL);
    if (!window)
    {
        cerr << "Error: No se pudo crear la ventana GLFW." << endl;
        glfwTerminate();
        return -1;
    }

    // Callbacks de GLFW
    glfwSetKeyCallback(window, onKeyCallback);
    glfwSetWindowSizeCallback(window, onWindowSizeCallback);
    glfwSetFramebufferSizeCallback(window, onFrameBufferSizeCallback);
    glfwSetWindowContentScaleCallback(window, onWindowContentScaleCallback);
    glfwSetMouseButtonCallback(window, onMouseButtonCallback);
    glfwSetCursorPosCallback(window, onCursorPosCallback);
    glfwSetScrollCallback(window, onScrollCallback);

    glfwGetFramebufferSize(window, &framebufferW, &framebufferH);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(swapInterval);

#ifdef GLEW_VERSION
    if (glewInit() != GLEW_OK)
    {
        cerr << "Error: No se pudo inicializar GLEW." << endl;
        glfwTerminate();
        return -1;
    }
#endif

    //--------------------------------------------------------------------------
    // 2. CREACIÓN DEL MUNDO VIRTUAL Y CÁMARA
    //--------------------------------------------------------------------------
    world = new cWorld();
    world->m_backgroundColor.set(0.08f, 0.09f, 0.12f); // Fondo azul grisáceo médico oscuro

    camera = new cCamera(world);
    world->addChild(camera);

    // Sistema esférico de navegación para la cámara apuntando al origen
    camera->setSphericalReferences(cVector3d(0.0, 0.0, 0.0),  // Origen (centro de articulación)
                                   cVector3d(0.0, 0.0, 1.0),  // Dirección Zenit (+Z)
                                   cVector3d(1.0, 0.0, 0.0)); // Dirección Azimut (+X)

    // Posición inicial: vista anterolateral a 0.50 metros
    camera->setSphericalDeg(0.50, 70.0, 35.0);
    camera->setClippingPlanes(0.01, 10.0);
    camera->setStereoMode(stereoMode);
    camera->setUseMultipassTransparency(true); // Soporte de transparencia multicapa

    // Iluminación
    lightPrincipal = new cDirectionalLight(world);
    camera->addChild(lightPrincipal);
    lightPrincipal->setEnabled(true);
    lightPrincipal->setDir(-1.0, -1.0, -0.8);
    lightPrincipal->m_ambient.set(0.35f, 0.35f, 0.38f);
    lightPrincipal->m_diffuse.set(0.85f, 0.85f, 0.85f);
    lightPrincipal->m_specular.set(0.95f, 0.95f, 0.95f);

    lightSecundaria = new cDirectionalLight(world);
    world->addChild(lightSecundaria);
    lightSecundaria->setEnabled(true);
    lightSecundaria->setDir(1.0, 0.8, -0.5);
    lightSecundaria->m_ambient.set(0.15f, 0.15f, 0.18f);
    lightSecundaria->m_diffuse.set(0.45f, 0.45f, 0.50f);
    lightSecundaria->m_specular.set(0.30f, 0.30f, 0.30f);

    //--------------------------------------------------------------------------
    // 3. DETECCIÓN Y CONFIGURACIÓN DEL DISPOSITIVO GEOMAGIC
    //--------------------------------------------------------------------------
    cout << "\n[HÁPTICA] Buscando dispositivos hápticos Geomagic / OpenHaptics..." << endl;
    handler = new cHapticDeviceHandler();
    unsigned int numDevices = handler->getNumDevices();
    cout << "[HÁPTICA] Dispositivos detectados por el sistema: " << numDevices << endl;

    bool deviceAcquired = handler->getDevice(hapticDevice, 0);
    if (deviceAcquired && hapticDevice)
    {
        hapticDevice->open();
        hapticDevice->calibrate();
        hapticDeviceInfo = hapticDevice->getSpecifications();

        cout << "[HÁPTICA] Dispositivo conectado: " << hapticDeviceInfo.m_modelName << endl;
        cout << "[HÁPTICA] Fabricante: " << hapticDeviceInfo.m_manufacturerName << endl;
        cout << "[HÁPTICA] Fuerza lineal máxima: " << hapticDeviceInfo.m_maxLinearForce << " N" << endl;
        cout << "[HÁPTICA] Rigidez lineal máxima: " << hapticDeviceInfo.m_maxLinearStiffness << " N/m" << endl;
        cout << "[HÁPTICA] Amortiguamiento máximo: " << hapticDeviceInfo.m_maxLinearDamping << " N/(m/s)" << endl;
        cout << "[HÁPTICA] Rotación sensada: " << (hapticDeviceInfo.m_sensedRotation ? "SI" : "NO") << endl;
    }
    else
    {
        cout << "[ADVERTENCIA] No se detectó Geomagic Touch activo en este momento." << endl;
        cout << "  -> Verifique el cable USB/Ethernet y la alimentación del dispositivo." << endl;
        cout << "  -> Verifique que 'Touch Diagnostic' o 'Touch Setup' reconozca el dispositivo." << endl;
        cout << "  -> CHAI3D utilizará un dispositivo virtual de prueba." << endl;
    }

    //--------------------------------------------------------------------------
    // 4. CREACIÓN DEL PUNTERO ESFÉRICO HÁPTICO (cToolCursor)
    //--------------------------------------------------------------------------
    tool = new cToolCursor(world);
    world->addChild(tool);
    tool->setHapticDevice(hapticDevice);

    // Radio de la esfera puntero (radio razonable para anatomía de hombro: 6 mm)
    tool->setRadius(toolRadius);

    // Mostrar únicamente la esfera proxy en contacto (ocultar esfera meta/dispositivo)
    tool->setShowContactPoints(true, false);

    // Material de la esfera puntero: Cian / Azul brillante con reflejo especular
    if (tool->m_hapticPoint && tool->m_hapticPoint->m_sphereProxy)
    {
        cMaterialPtr toolMat = cMaterial::create();
        toolMat->setBlueCyan();
        toolMat->m_ambient.set(0.1f, 0.4f, 0.7f, 1.0f);
        toolMat->m_diffuse.set(0.2f, 0.7f, 1.0f, 1.0f);
        toolMat->m_specular.set(1.0f, 1.0f, 1.0f, 1.0f);
        toolMat->setShininess(100);
        tool->m_hapticPoint->m_sphereProxy->setMaterial(toolMat);
    }

    // Mapeo del espacio de trabajo del Geomagic al tamaño de la escena (~0.30 m)
    tool->setWorkspaceRadius(0.30);

    // Orientar la herramienta en consonancia con la cámara
    tool->setLocalRot(camera->getLocalRot());

    // Evitar sacudidas de fuerza al inicio
    tool->setWaitForSmallForce(true);

    // Iniciar herramienta háptica
    tool->start();

    //--------------------------------------------------------------------------
    // 5. CARGA DEL MODELO ANATÓMICO DEL HOMBRO
    //--------------------------------------------------------------------------
    shoulderMesh = new cMultiMesh();
    world->addChild(shoulderMesh);

    string currentpath = cGetCurrentPath();

    // Lista de rutas posibles donde se encuentra el modelo OBJ
    vector<string> rutasModelo = {
        currentpath + "/../../../../modelo/Hombro_Anatomico_Inyeccion.obj",
        currentpath + "/../resources/models/hombro/Hombro_Anatomico_Inyeccion.obj",
        currentpath + "/../../bin/resources/models/hombro/Hombro_Anatomico_Inyeccion.obj",
        "Hombro_Anatomico_Inyeccion.obj"
    };

    bool modeloCargado = false;
    string rutaEfectiva = "";
    for (const auto& ruta : rutasModelo)
    {
        cout << "[MODELO] Intentando cargar: " << ruta << " ... " << flush;
        if (shoulderMesh->loadFromFile(ruta))
        {
            modeloCargado = true;
            rutaEfectiva = ruta;
            cout << "OK!" << endl;
            break;
        }
        else
        {
            cout << "No encontrado." << endl;
        }
    }

    if (!modeloCargado)
    {
        cerr << "\n[ERROR CRÍTICO] No se pudo cargar el modelo anatómico del hombro." << endl;
        cerr << "Verifique la ruta G:\\Mi unidad\\Investigación\\Haptico\\Inyección intraarticular\\modelo\\Hombro_Anatomico_Inyeccion.obj" << endl;
        closeApp();
        return -1;
    }

    // Centrar el modelo en el origen para que la articulación quede en (0, 0, 0)
    shoulderMesh->setLocalPos(-SHOULDER_CENTER);

    // Desactivar culling de caras para que ambas caras de las mallas sean visibles y palpables
    shoulderMesh->setUseCulling(false);

    // Calcular cajas de contorno
    shoulderMesh->computeBoundaryBox(true);

    // Calcular rigidez física disponible
    double workspaceScaleFactor = tool->getWorkspaceScaleFactor();
    double maxDeviceStiffness = hapticDeviceInfo.m_maxLinearStiffness;
    if (maxDeviceStiffness <= 0.0) maxDeviceStiffness = 1000.0; // Valor nominal por defecto si es simulado
    double maxVirtualStiffness = maxDeviceStiffness / workspaceScaleFactor;

    cout << "\n[HÁPTICA] Configurando propiedades de materiales y colisión AABB..." << endl;
    cout << "  - Sub-mallas en el modelo: " << shoulderMesh->getNumMeshes() << endl;
    cout << "  - Vértices totales: " << shoulderMesh->getNumVertices() << endl;

    // Crear árbol de detección de colisiones AABB para respuesta háptica rápida a 1 kHz
    shoulderMesh->createAABBCollisionDetector(toolRadius);

    // Configuración háptica de rigidez y fricción
    shoulderMesh->setStiffness(stiffnessFactor * maxVirtualStiffness, true);
    shoulderMesh->setFriction(0.12, 0.18, true);

    cout << "[HÁPTICA] Árbol AABB generado con éxito. Modelo listo para interacción." << endl;

    //--------------------------------------------------------------------------
    // 6. CREACIÓN DE LA INTERFAZ EN PANTALLA (HUD)
    //--------------------------------------------------------------------------
    fontTitle = NEW_CFONT_CONSOLAS_18();
    fontInfo = NEW_CFONT_CONSOLAS_16();
    fontHelp = NEW_CFONT_CONSOLAS_16();

    labelTitle = new cLabel(fontTitle);
    labelTitle->m_fontColor.set(0.2f, 0.85f, 1.0f);
    labelTitle->setText("SIMULADOR HÁPTICO: PALPACIÓN DE HOMBRO PARA INYECCIÓN ARTICULAR");
    camera->m_frontLayer->addChild(labelTitle);

    labelDevice = new cLabel(fontInfo);
    labelDevice->m_fontColor.set(0.9f, 0.9f, 0.9f);
    string devText = "Dispositivo: " + (hapticDeviceInfo.m_modelName.empty() ? string("Virtual / Emulado") : hapticDeviceInfo.m_modelName);
    labelDevice->setText(devText);
    camera->m_frontLayer->addChild(labelDevice);

    labelPosition = new cLabel(fontInfo);
    labelPosition->m_fontColor.set(0.85f, 0.85f, 0.85f);
    camera->m_frontLayer->addChild(labelPosition);

    labelForce = new cLabel(fontInfo);
    labelForce->m_fontColor.set(1.0f, 0.8f, 0.2f);
    camera->m_frontLayer->addChild(labelForce);

    labelRates = new cLabel(fontInfo);
    labelRates->m_fontColor.set(0.4f, 1.0f, 0.4f);
    camera->m_frontLayer->addChild(labelRates);

    labelHelp = new cLabel(fontHelp);
    labelHelp->m_fontColor.set(0.7f, 0.7f, 0.7f);
    labelHelp->setText("[1] Alternar deltoides  |  [2-5] Vistas clínicas (Ant/Lat/Post/Sup)\n[+/-] Rigidez háptica  |  [[/]] Tamaño esfera puntero  |  [C] Calibrar  |  [H] Ayuda");
    camera->m_frontLayer->addChild(labelHelp);

    // Viewport
    float contentScaleW, contentScaleH;
    glfwGetWindowContentScale(window, &contentScaleW, &contentScaleH);
    viewport = new cViewport(camera, contentScaleW, contentScaleH);

    // Posicionamiento inicial del HUD
    updateHUD();

    //--------------------------------------------------------------------------
    // 7. INICIO DEL HILO HÁPTICO A ALTA FRECUENCIA (1000 HZ)
    //--------------------------------------------------------------------------
    hapticsThread = new cThread();
    hapticsThread->start(renderHaptics, CTHREAD_PRIORITY_HAPTICS);
    atexit(closeApp);

    //--------------------------------------------------------------------------
    // 8. BUCLE PRINCIPAL DE RENDERIZADO GRÁFICO
    //--------------------------------------------------------------------------
    cout << "\n[SISTEMA] Simulación iniciada correctamente." << endl;
    cout << "  -> Mueva el lápiz del Geomagic Touch para tocar las estructuras del hombro." << endl;
    cout << "  -> Arrastre con el botón izquierdo del ratón para rotar la cámara." << endl;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        renderGraphics();
        glfwSwapBuffers(window);
    }

    // Salir
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

//==============================================================================
// ACTUALIZACIÓN DE INTERFAZ EN PANTALLA (HUD)
//==============================================================================

void updateHUD()
{
    if (!labelTitle || !viewport) return;

    int w = framebufferW;
    int h = framebufferH;

    labelTitle->setLocalPos(25, h - 35, 0);
    labelDevice->setLocalPos(25, h - 60, 0);
    labelPosition->setLocalPos(25, h - 85, 0);
    labelForce->setLocalPos(25, h - 110, 0);
    labelRates->setLocalPos(25, h - 135, 0);
    labelHelp->setLocalPos(25, 20, 0);
}

//==============================================================================
// BUCLE DE RENDERIZADO GRÁFICO (Hilo Principal)
//==============================================================================

void renderGraphics()
{
    freqCounterGraphics.signal(1);

    if (showHUD)
    {
        // Leer posición actual del puntero en mm
        cVector3d pos = tool->getDeviceGlobalPos() * 1000.0;
        char posStr[128];
        snprintf(posStr, sizeof(posStr), "Posición puntero: X: %+.1f mm | Y: %+.1f mm | Z: %+.1f mm | Radio: %.1f mm",
                 pos.x(), pos.y(), pos.z(), toolRadius * 1000.0);
        labelPosition->setText(posStr);

        // Leer fuerza calculada en Newtons
        cVector3d force = tool->getDeviceGlobalForce();
        double mag = force.length();
        char forceStr[128];
        snprintf(forceStr, sizeof(forceStr), "Fuerza háptica: |F| = %.2f N (Fx: %+.2f, Fy: %+.2f, Fz: %+.2f N)",
                 mag, force.x(), force.y(), force.z());
        labelForce->setText(forceStr);

        // Frecuencia gráfica y háptica
        char ratesStr[128];
        snprintf(ratesStr, sizeof(ratesStr), "Gráficos: %.0f FPS  |  Háptica: %.0f Hz  |  Rigidez: %.0f%%",
                 freqCounterGraphics.getFrequency(), freqCounterHaptics.getFrequency(), stiffnessFactor * 100.0);
        labelRates->setText(ratesStr);
    }

    // Renderizar escena a través de la cámara
    camera->renderView(framebufferW, framebufferH);
}

//==============================================================================
// BUCLE HÁPTICO (Hilo Dedicado a ~1000 Hz)
//==============================================================================

void renderHaptics()
{
    simulationRunning = true;
    simulationFinished = false;

    while (simulationRunning)
    {
        freqCounterHaptics.signal(1);

        // 1. Leer posición, orientación y estado de botones desde el Geomagic
        tool->updateFromDevice();

        // 2. Calcular algoritmo de colisión god-object/proxy con la malla del hombro
        tool->computeInteractionForces();

        // 3. Enviar la fuerza calculada a los motores del Geomagic Touch
        tool->applyToDevice();
    }

    simulationFinished = true;
}

//==============================================================================
// CONFIGURACIÓN DE VISTAS CLÍNICAS PREDEFINIDAS
//==============================================================================

void setCameraView(int a_viewIndex)
{
    if (!camera) return;

    switch (a_viewIndex)
    {
        case 1: // Vista Anterior (Frontal estándar)
            camera->setSphericalDeg(0.50, 85.0, 0.0);
            cout << "> Vista Anterior (Frontal)" << endl;
            break;
        case 2: // Vista Lateral (Óptima para abordaje subacromial de inyección)
            camera->setSphericalDeg(0.50, 75.0, 70.0);
            cout << "> Vista Lateral (Abordaje subacromial)" << endl;
            break;
        case 3: // Vista Posterior
            camera->setSphericalDeg(0.50, 85.0, 180.0);
            cout << "> Vista Posterior" << endl;
            break;
        case 4: // Vista Superior / Cefálica (Espacio acromioclavicular)
            camera->setSphericalDeg(0.48, 15.0, 45.0);
            cout << "> Vista Superior (Cefálica)" << endl;
            break;
    }

    // Alinear orientación de la herramienta con la cámara
    if (tool)
    {
        tool->setLocalRot(camera->getLocalRot());
    }
}

//==============================================================================
// CALLBACKS DE INTERACCIÓN (TECLADO, RATÓN, VENTANA)
//==============================================================================

void onKeyCallback(GLFWwindow* a_window, int a_key, int a_scancode, int a_action, int a_mods)
{
    if (a_action != GLFW_PRESS && a_action != GLFW_REPEAT) return;

    // Salir (ESC o Q)
    if (a_key == GLFW_KEY_ESCAPE || a_key == GLFW_KEY_Q)
    {
        glfwSetWindowShouldClose(a_window, GLFW_TRUE);
    }

    // [1] Alternar visibilidad / transparencia del deltoides
    else if (a_key == GLFW_KEY_1)
    {
        deltoidVisible = !deltoidVisible;
        if (shoulderMesh)
        {
            // Ajustar transparencia o visibilidad de la capa muscular externa
            for (int i = 0; i < shoulderMesh->getNumMeshes(); i++)
            {
                cMesh* m = shoulderMesh->getMesh(i);
                // Si la malla contiene material o nombre del deltoides
                if (m && m->m_material)
                {
                    float currentAlpha = m->m_material->m_diffuse.getA();
                    if (currentAlpha < 0.8f) // Músculo translúcido
                    {
                        m->setShowEnabled(deltoidVisible);
                        m->setHapticEnabled(deltoidVisible);
                    }
                }
            }
        }
        cout << "> Músculo deltoides externo: " << (deltoidVisible ? "ACTIVADO" : "OCULTO (acceso a bursa)") << endl;
    }

    // [2 - 5] Vistas clínicas anatómicas
    else if (a_key == GLFW_KEY_2) setCameraView(1);
    else if (a_key == GLFW_KEY_3) setCameraView(2);
    else if (a_key == GLFW_KEY_4) setCameraView(3);
    else if (a_key == GLFW_KEY_5) setCameraView(4);

    // [C] Calibrar dispositivo háptico
    else if (a_key == GLFW_KEY_C)
    {
        if (hapticDevice)
        {
            hapticDevice->calibrate();
            cout << "> Dispositivo Geomagic calibrado." << endl;
        }
    }

    // [R] Reiniciar orientación del cursor con la cámara
    else if (a_key == GLFW_KEY_R)
    {
        if (tool && camera)
        {
            tool->setLocalRot(camera->getLocalRot());
            cout << "> Puntero háptico reorientado con la cámara." << endl;
        }
    }

    // [H] Alternar HUD
    else if (a_key == GLFW_KEY_H)
    {
        showHUD = !showHUD;
        camera->m_frontLayer->setShowEnabled(showHUD);
    }

    // [[ / ]] Aumentar / Disminuir radio del puntero esférico
    else if (a_key == GLFW_KEY_RIGHT_BRACKET) // ']'
    {
        toolRadius = cClamp(toolRadius + 0.001, 0.002, 0.025);
        if (tool) tool->setRadius(toolRadius);
        cout << "> Radio del puntero esférico: " << toolRadius * 1000.0 << " mm" << endl;
    }
    else if (a_key == GLFW_KEY_LEFT_BRACKET) // '['
    {
        toolRadius = cClamp(toolRadius - 0.001, 0.002, 0.025);
        if (tool) tool->setRadius(toolRadius);
        cout << "> Radio del puntero esférico: " << toolRadius * 1000.0 << " mm" << endl;
    }

    // [+ / -] Aumentar / Disminuir rigidez háptica
    else if (a_key == GLFW_KEY_EQUAL || a_key == GLFW_KEY_KP_ADD) // '+'
    {
        stiffnessFactor = cClamp(stiffnessFactor + 0.05, 0.05, 1.0);
        double maxK = (hapticDeviceInfo.m_maxLinearStiffness > 0.0 ? hapticDeviceInfo.m_maxLinearStiffness : 1000.0) / tool->getWorkspaceScaleFactor();
        if (shoulderMesh) shoulderMesh->setStiffness(stiffnessFactor * maxK, true);
        cout << "> Rigidez háptica ajustada: " << stiffnessFactor * 100.0 << "%" << endl;
    }
    else if (a_key == GLFW_KEY_MINUS || a_key == GLFW_KEY_KP_SUBTRACT) // '-'
    {
        stiffnessFactor = cClamp(stiffnessFactor - 0.05, 0.05, 1.0);
        double maxK = (hapticDeviceInfo.m_maxLinearStiffness > 0.0 ? hapticDeviceInfo.m_maxLinearStiffness : 1000.0) / tool->getWorkspaceScaleFactor();
        if (shoulderMesh) shoulderMesh->setStiffness(stiffnessFactor * maxK, true);
        cout << "> Rigidez háptica ajustada: " << stiffnessFactor * 100.0 << "%" << endl;
    }
}

void onMouseButtonCallback(GLFWwindow* a_window, int a_button, int a_action, int a_mods)
{
    if (a_action == GLFW_PRESS)
    {
        glfwGetCursorPos(a_window, &mouseX, &mouseY);
        if (a_button == GLFW_MOUSE_BUTTON_LEFT)
        {
            mouseState = MOUSE_ROTATE_CAMERA;
        }
        else if (a_button == GLFW_MOUSE_BUTTON_RIGHT)
        {
            mouseState = MOUSE_PAN_CAMERA;
        }
    }
    else
    {
        mouseState = MOUSE_IDLE;
    }
}

void onCursorPosCallback(GLFWwindow* a_window, double a_posX, double a_posY)
{
    double dx = a_posX - mouseX;
    double dy = a_posY - mouseY;
    mouseX = a_posX;
    mouseY = a_posY;

    if (mouseState == MOUSE_ROTATE_CAMERA && camera)
    {
        double azimut = camera->getSphericalAzimuthDeg() - 0.4 * dx;
        double polar = cClamp(camera->getSphericalPolarDeg() - 0.4 * dy, 5.0, 175.0);
        camera->setSphericalAzimuthDeg(azimut);
        camera->setSphericalPolarDeg(polar);

        if (tool) tool->setLocalRot(camera->getLocalRot());
    }
    else if (mouseState == MOUSE_PAN_CAMERA && camera)
    {
        cVector3d look = camera->getLookVector();
        cVector3d up = camera->getUpVector();
        cVector3d right = camera->getRightVector();

        cVector3d target = camera->getSphericalOriginReference();
        target = target - 0.0005 * dx * right + 0.0005 * dy * up;
        camera->setSphericalOriginReference(target);
    }
}

void onScrollCallback(GLFWwindow* a_window, double a_offsetX, double a_offsetY)
{
    if (!camera) return;
    double r = camera->getSphericalRadius();
    r = cClamp(r - 0.04 * a_offsetY, 0.15, 2.50);
    camera->setSphericalRadius(r);
}

void onWindowSizeCallback(GLFWwindow* a_window, int a_width, int a_height)
{
    windowW = a_width;
    windowH = a_height;
    updateHUD();
    renderGraphics();
}

void onFrameBufferSizeCallback(GLFWwindow* a_window, int a_width, int a_height)
{
    framebufferW = a_width;
    framebufferH = a_height;
    updateHUD();
}

void onWindowContentScaleCallback(GLFWwindow* a_window, float a_xscale, float a_yscale)
{
    if (viewport) viewport->setContentScale(a_xscale, a_yscale);
    updateHUD();
}

void onErrorCallback(int a_error, const char* a_description)
{
    cerr << "[GLFW ERROR] " << a_description << endl;
}

//==============================================================================
// CIERRE Y LIBERACIÓN DE RECURSOS
//==============================================================================

void closeApp()
{
    // Detener bucle háptico
    simulationRunning = false;
    while (!simulationFinished)
    {
        cSleepMs(20);
    }

    // Detener y cerrar herramienta y dispositivo
    if (tool)
    {
        tool->stop();
    }
    if (hapticDevice)
    {
        hapticDevice->close();
    }

    // Eliminar memoria de objetos
    delete hapticsThread;
    delete world;
    delete handler;
}
