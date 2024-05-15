// Slider to control rotation speed
// Add more cubes

#include <windows.h>
#include <string>
#include <sstream>
#include <iomanip>

#include "Resource.h"

#undef small
#ifdef _M_ARM64
#include <armpl.h>
#else
#include <cmath>

#endif

#define _USE_MATH_DEFINES
#include <math.h>
#include <vector>

BOOL useAPL = false;
BOOL closeThreads = false;
const int scale = 40;
const int spherePoints = 1000;

// Define the window title
const wchar_t g_szClassName[] = L"myWindowClass";

// Angle of rotation
double rotationAngle = 0.0;
HPEN whitepen = CreatePen(PS_SOLID, 4, 0x00ffffff);
HFONT hfon = 0;

std::vector<HANDLE> thread_handles;

CRITICAL_SECTION cubeDraw = { 0 };

// Speed counter
int Calculations = 0;
DWORD startTime = 0;

// Define a simple structure for 3D points
struct Point3D 
{
    double x, y, z;
};

// Define the rotation matrix 
std::vector<double> rotationInX = {
    0,            0,           1,
    cos(M_PI / 2), -sin(M_PI / 2), 0,
    sin(M_PI / 2),  cos(M_PI / 2), 0
};

//// Define a cube by its vertices
//std::vector<Point3D> cubeVertices = {
//    {1 * scale, 1 * scale, 1 * scale}, {-1 * scale, 1 * scale, 1 * scale}, {-1* scale, -1 * scale, 1 * scale}, {1 * scale, -1 * scale, 1 * scale},
//    {1 * scale, 1 * scale, -1 * scale}, {-1 * scale, 1 * scale, -1 * scale}, {-1 * scale, -1 * scale, -1 * scale}, {1 * scale, -1 * scale, -1 * scale}
//};

// Define a cube by its vertices
std::vector<double> cubeVertices = {
    1 * scale, 1 * scale, 1 * scale, -1 * scale, 1 * scale, 1 * scale, -1 * scale, -1 * scale, 1 * scale, 1 * scale, -1 * scale, 1 * scale,
    1 * scale, 1 * scale, -1 * scale, -1 * scale, 1 * scale, -1 * scale, -1 * scale, -1 * scale, -1 * scale, 1 * scale, -1 * scale, -1 * scale
};

std::vector<double> sphereVertices;

std::vector<double> generateSpherePoints(int numPoints) 
{
    std::vector<double> points;
    const double goldenRatio = (1 + std::sqrt(5.0)) / 2;
    const double angleIncrement = M_PI * 2 * goldenRatio;

    for (int i = 0; i < numPoints; ++i) 
    {
        double t = (double)i / numPoints;
        double inclination = std::acos(1 - 2 * t);
        double azimuth = angleIncrement * i;

        double x = std::sin(inclination) * std::cos(azimuth);
        double y = std::sin(inclination) * std::sin(azimuth);
        double z = std::cos(inclination);

        points.push_back(x);
        points.push_back(y);
        points.push_back(z);
    }

    return points;
}


// Function to apply a rotation matrix to a matrix of 3D points using C operations
void applyRotation(std::vector<double> &shape, const std::vector<double>& rotMatrix)
{
    EnterCriticalSection(&cubeDraw);

    auto point = shape.begin();
    while (point != shape.end())
    {

        // read three points
        Point3D startPoint;
        startPoint.x = *point; point++;
        startPoint.y = *point; point++;
        startPoint.z = *point;

        // go back two points
        point -= 2;

        Point3D rotatedPoint;
        rotatedPoint.x = rotMatrix[0] * startPoint.x + rotMatrix[1] * startPoint.y + rotMatrix[2] * startPoint.z;
        rotatedPoint.y = rotMatrix[3] * startPoint.x + rotMatrix[4] * startPoint.y + rotMatrix[5] * startPoint.z;
        rotatedPoint.z = rotMatrix[6] * startPoint.x + rotMatrix[7] * startPoint.y + rotMatrix[8] * startPoint.z;

        *point = rotatedPoint.x; point++;
        *point = rotatedPoint.y; point++;
        *point = rotatedPoint.z; point++;
    }

    Calculations++;

    LeaveCriticalSection(&cubeDraw);
}

void applyRotationBLAS(std::vector<double>& shape, const std::vector<double>& rotMatrix)
{
    EnterCriticalSection(&cubeDraw);
    double * newCubeVertices = new double[shape.size()];

#ifdef _M_ARM64
        cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, shape.size() / 3, 3, 3, 1.0, shape.data(), 3, rotMatrix.data(), 3, 0.0, newCubeVertices, 3);
        memcpy(shape.data(),newCubeVertices, shape.size() * sizeof(double));
#endif

    Calculations++;

    LeaveCriticalSection(&cubeDraw);
}

// Function to draw a cube
void DrawCube(HDC hdc, RECT cliRect)
{
    static int delayCounter = 0;
    static wchar_t msg[1024] = { 0 };
    POINT centre = { (cliRect.right - cliRect.left) / 2, (cliRect.bottom - cliRect.top) / 2 };
    if (hfon == 0)
    {
        hfon = CreateFont(
            -MulDiv(24, GetDeviceCaps(hdc, LOGPIXELSY), 72),
            0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"ARIAL");
    }

    SetTextColor(hdc, 0x00FFFFFF);
    SetBkColor(hdc, 0x00000000);
    auto oldfon = SelectObject(hdc, hfon);
    auto oldPen = SelectObject(hdc, whitepen);

    EnterCriticalSection(&cubeDraw);
    // Draw new
    MoveToEx(hdc, cubeVertices[0] + centre.x, cubeVertices[1] + centre.y, NULL);
    LineTo(hdc, ( cubeVertices[3]) + centre.x, ( cubeVertices[4]) + centre.y);
    LineTo(hdc, ( cubeVertices[6]) + centre.x, ( cubeVertices[7]) + centre.y);
    LineTo(hdc, ( cubeVertices[9]) + centre.x, ( cubeVertices[10]) + centre.y);
    LineTo(hdc, ( cubeVertices[0]) + centre.x, ( cubeVertices[1]) + centre.y);

    MoveToEx(hdc, ( cubeVertices[12]) + centre.x, ( cubeVertices[13]) + centre.y, NULL);
    LineTo(hdc, ( cubeVertices[15]) + centre.x, ( cubeVertices[16]) + centre.y);
    LineTo(hdc, ( cubeVertices[18]) + centre.x, ( cubeVertices[19]) + centre.y);
    LineTo(hdc, ( cubeVertices[21]) + centre.x, ( cubeVertices[22]) + centre.y);
    LineTo(hdc, ( cubeVertices[12]) + centre.x, ( cubeVertices[13]) + centre.y);

    MoveToEx(hdc, ( cubeVertices[0]) + centre.x, ( cubeVertices[1]) + centre.y, NULL);
    LineTo(hdc, ( cubeVertices[12]) + centre.x, ( cubeVertices[13]) + centre.y);
    MoveToEx(hdc, ( cubeVertices[3]) + centre.x, ( cubeVertices[4]) + centre.y, NULL);
    LineTo(hdc, ( cubeVertices[15]) + centre.x, ( cubeVertices[16]) + centre.y);
    MoveToEx(hdc, ( cubeVertices[6]) + centre.x, ( cubeVertices[7]) + centre.y, NULL);
    LineTo(hdc, ( cubeVertices[18]) + centre.x, ( cubeVertices[19]) + centre.y);
    MoveToEx(hdc, ( cubeVertices[9]) + centre.x, ( cubeVertices[10]) + centre.y, NULL);
    LineTo(hdc, ( cubeVertices[21]) + centre.x, ( cubeVertices[22]) + centre.y);

    // Calculate and draw the rate.
    if (delayCounter == 0)
    {
        DWORD now = GetTickCount();
        DWORD diff = now - startTime;
        startTime = now;

        int calcPerSecond = (Calculations * 1000.0) / (diff * 1.0);
        Calculations = 0;
        std::wstringstream stream;
        stream << L"1k Ops/s: " << std::fixed << std::setprecision(2) << calcPerSecond / 1000.0;
        wsprintf(msg, stream.str().c_str());
    }

    delayCounter++;
    if (delayCounter == 50)
    {
        delayCounter = 0;
    }

    DrawText(hdc, msg, -1, &cliRect, DT_LEFT);

    LeaveCriticalSection(&cubeDraw);

    SelectObject(hdc, oldfon);
    SelectObject(hdc, oldPen);
}

void RotateCube()
{
    // Update the angle of rotation
//    rotationAngle += 0.0000000000000001;
    rotationAngle += 0.000000000001;
    if (rotationAngle > 2 * M_PI)
        rotationAngle -= 2 * M_PI;

    // rotate
    rotationInX[3] = cos(rotationAngle);
    rotationInX[4] = -sin(rotationAngle);
    rotationInX[6] = sin(rotationAngle);
    rotationInX[7] = cos(rotationAngle);

    if (useAPL)
    {
        applyRotationBLAS(cubeVertices, rotationInX);
    }
    else
    {
        applyRotation(cubeVertices, rotationInX);
    }
}

DWORD WINAPI ThreadProc(LPVOID cubeID)
{
    while (!closeThreads)
    {
        RotateCube();
    }

    return 0;
}

// Window procedure function
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        // Parse the menu selections:
        switch (wmId)
        {
        case ID_OPTIONS_USE:
            useAPL = !useAPL;

            // Set the check
            CheckMenuItem(GetMenu(hwnd) , ID_OPTIONS_USE, (useAPL ? MF_CHECKED : 0));
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
    }
    break;


    case WM_PAINT:
    {
        RECT rectCli = { 0 };
        GetClientRect(hwnd, &rectCli);

        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // Create a memory DC compatible with the screen DC
        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdc, rectCli.right- rectCli.left, rectCli.bottom - rectCli.top);

        // Select the bitmap into the off-screen DC
        HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

        // Perform drawing operations here using hdcMem...
        DrawCube(hdcMem, rectCli);

        // Transfer the off-screen DC to the screen
        BitBlt(hdc, 0, 0, rectCli.right - rectCli.left, rectCli.bottom - rectCli.top, hdcMem, 0, 0, SRCCOPY);

        // Clean up
        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);

        EndPaint(hwnd, &ps);
    }
    break;

    case WM_ERASEBKGND:
        return TRUE; 

    case WM_TIMER:
    {
        // Invalidate the window to trigger a WM_PAINT for redrawing
        InvalidateRect(hwnd, NULL, FALSE);
    }
    break;
    case WM_CREATE:
    {
        // Set a timer to update the animation
        startTime = GetTickCount();
        SetTimer(hwnd, 1, 50, NULL);
        thread_handles.push_back(CreateThread(NULL, 0, ThreadProc, (LPVOID)1, 0, NULL));
    }
    break;

    case WM_DESTROY:
    {
        closeThreads = TRUE;
        Sleep(20);
        PostQuitMessage(0);
    }
    break;

    case WM_SIZE:
        InvalidateRect(hwnd, NULL, TRUE);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// Main function
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSEX wc;
    HWND hwnd;
    MSG Msg;

    InitializeCriticalSection(&cubeDraw);
    sphereVertices = generateSpherePoints(spherePoints);

    // Registering the Window Class
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = 0;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = L"Options";
    wc.lpszMenuName = MAKEINTRESOURCEW(IDC_SPINTHECUBEINGDI);
    wc.lpszClassName = g_szClassName;
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wc))
    {
        MessageBox(NULL, L"Window Registration Failed!", L"Error!",
            MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // Creating the Window
    hwnd = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        g_szClassName,
        L"Spinning Cube Demo",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 480 , 420,
        NULL, NULL, hInstance, NULL);

    if (hwnd == NULL)
    {
        MessageBox(NULL, L"Window Creation Failed!", L"Error!",
            MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // The Message Loop
    while (GetMessage(&Msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&Msg);
        DispatchMessage(&Msg);
    }

    DeleteCriticalSection(&cubeDraw);

    return (UINT) Msg.wParam;
}
