// split the calculations out onto multi threads
// start event / semaphore and an end even / semaphore


#include <windows.h>
#include <string>
#include <sstream>
#include <iomanip>

#include "Resource.h"

#undef small
//#define _USE_ARMPL_DEFINES

#if defined(_M_ARM64) && defined(_USE_ARMPL_DEFINES)
#include <armpl.h>
#endif

#define _USE_MATH_DEFINES
#include <math.h>
#include <vector>

BOOL UseCube = false;
BOOL useAPL = false;
BOOL closeThreads = false;
const int ThreadCount = 7;
ULONG timeVal = 0;
ULONG timeValBLAS = 0;
std::vector<HANDLE> semaphoreList;
std::vector<HANDLE> doneList;


const double scale = 240;
const int spherePoints = 40000;
const int stride = spherePoints/2000;

static wchar_t rateMsg[1024] = { 0 };

// Define the window title
const wchar_t g_szClassName[] = L"myWindowClass";

// Angle of rotation
double rotationAngle = 0.0;
HPEN whitepen = CreatePen(PS_SOLID, 1, 0x00ffffff);
HFONT hfon = 0;

std::vector<HANDLE> thread_handles;
CRITICAL_SECTION cubeDraw[ThreadCount];

// Speed counter
int Calculations = 0;
DWORD startTime = 0;

// Define a simple structure for 3D points
struct Point3D 
{
    double x, y, z;
};

// Define the rotation matrix 
std::vector<double> rotationInX = 
{
    0,0,1,
    0,1,0, 
    1,0,0
};

// Define a cube by its vertices
std::vector<double> cubeVertices = {
    1 , 1 , 1 , -1 , 1 , 1 , -1 , -1 , 1 , 1 , -1 , 1 ,
    1 , 1 , -1 , -1 , 1 , -1 , -1 , -1 , -1 , 1 , -1 , -1 
};

std::vector<double> sphereVertices;
std::vector<double> drawSphereVertecies;

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

// Apply a rotation matrix to the list of 3D verticies using C operators
void applyRotation(std::vector<double>& shape, const std::vector<double>& rotMatrix, int startPoint, int stride)
{
    double refx, refy, refz;

    // Start looking at the reference verticies 
    auto point = shape.begin();
    point += startPoint * 3;

    // Start the output transformed verticies 
    auto outpoint = drawSphereVertecies.begin();
    outpoint += startPoint * 3;

    int counter = 0;
    while (point != shape.end() && counter < stride)
    {
        counter++;

        // take the next three values for a 3d point
        refx = *point; point++;
        refy = *point; point++;
        refz = *point; point++;

        // do the matrix multiplication and scale
        // [ x, y, z]     [ rotation 0, rotation 1, rotation 2]    [newx, newy, newz]
        //            dot [ rotation 3, rotation 4, rotation 5]  = 
        //                [ rotation 6, rotation 7, rotation 8]     

        *outpoint = scale * rotMatrix[0] * refx + 
                    scale * rotMatrix[3] * refy + 
                    scale * rotMatrix[6] * refz; outpoint++;

        *outpoint = scale * rotMatrix[1] * refx + 
                    scale * rotMatrix[4] * refy + 
                    scale * rotMatrix[7] * refz; outpoint++;

        *outpoint = scale * rotMatrix[2] * refx + 
                    scale * rotMatrix[5] * refy + 
                    scale * rotMatrix[8] * refz; outpoint++;
    }
}

// Apply a rotation matrix to the list of 3D verticies using Arm Performance Libraries
// BLAS implementation for double precision 
void applyRotationBLAS(std::vector<double>& shape, const std::vector<double>& rotMatrix)
{
    EnterCriticalSection(&cubeDraw[0]);
#if defined(_M_ARM64) && defined(_USE_ARMPL_DEFINES)
    // Call the BLAS matrix mult for doubles. 
    // Multiplies each of the 3d points in shape 
    // list with rotation matrix, and applies scale
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, (int)shape.size() / 3, 3, 3, scale, shape.data(), 3, rotMatrix.data(), 3, 0.0, drawSphereVertecies.data(), 3);
#endif
    LeaveCriticalSection(&cubeDraw[0]);
}

// increment the rotation matrix
void RotateCube(int numCores)
{
    rotationAngle += 0.00001;
    if (rotationAngle > 2 * M_PI)
    {
        rotationAngle -= 2 * M_PI;
    }

    // rotate around Z and Y
    rotationInX[0] = cos(rotationAngle) * cos(rotationAngle);
    rotationInX[1] = -sin(rotationAngle);
    rotationInX[2] = cos(rotationAngle) * sin(rotationAngle);
    rotationInX[3] = sin(rotationAngle) * cos(rotationAngle);
    rotationInX[4] = cos(rotationAngle);
    rotationInX[5] = sin(rotationAngle) * sin(rotationAngle);
    rotationInX[6] = -sin(rotationAngle);
    rotationInX[7] = 0;
    rotationInX[8] = cos(rotationAngle);


    if (useAPL)
    {
        applyRotationBLAS(UseCube ? cubeVertices : sphereVertices, rotationInX);
    }
    else
    {
        for (int x = 0; x < numCores; x++)
        {
            ReleaseSemaphore(semaphoreList[x], 1, NULL);
        }
        WaitForMultipleObjects(numCores, doneList.data(), TRUE, INFINITE);
    }

    Calculations++;
}


DWORD WINAPI CalcThreadProc(LPVOID data)
{
    // need to know where to start and where to end
    int threadNum = LOWORD(data);
    int threadCount = HIWORD(data);
    int pointStride = spherePoints / threadCount;

    while (!closeThreads)
    {
        // wait on a semaphore
        WaitForSingleObject(semaphoreList[threadNum], INFINITE);

        EnterCriticalSection(&cubeDraw[threadNum]);
        // run the calculations for the set of points - need to be global
        applyRotation(UseCube ? cubeVertices : sphereVertices, rotationInX, threadNum * pointStride, pointStride);
        LeaveCriticalSection(&cubeDraw[threadNum]);

        // set a semaphore to say we are done
        ReleaseSemaphore(doneList[threadNum], 1, NULL);
    }

    return 0;
}


DWORD WINAPI ThreadProc(LPVOID numCores)
{
    int coreCount = static_cast<int>(reinterpret_cast<std::uintptr_t>(numCores));

    // Check how many cores there are and spin that number of threads
    for (int count =0; count < coreCount;count++)
	{
        semaphoreList.push_back(CreateSemaphore(NULL, 0,1,NULL));
        doneList.push_back(CreateSemaphore(NULL, 0, 1, NULL));

		thread_handles.push_back(CreateThread(NULL, 0, CalcThreadProc, (LPVOID) MAKELPARAM(count, numCores), 0, NULL));
	}

    while (!closeThreads)
    {
        RotateCube(coreCount);
    }

    return 0;
}

// Function to draw a cube
void DrawCube(HDC hdc, RECT cliRect)
{
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

    if (useAPL)
    {
        EnterCriticalSection(&cubeDraw[0]);
    }
    else
    {
        for (int x = 0; x < ThreadCount; x++)
        {
            EnterCriticalSection(&cubeDraw[x]);
        }
    }

    // Draw new
    if (UseCube)
    {
        MoveToEx(hdc, drawSphereVertecies[0] + centre.x, drawSphereVertecies[1] + centre.y, NULL);
        LineTo(hdc, (drawSphereVertecies[3]) + centre.x, (drawSphereVertecies[4]) + centre.y);
        LineTo(hdc, (drawSphereVertecies[6]) + centre.x, (drawSphereVertecies[7]) + centre.y);
        LineTo(hdc, (drawSphereVertecies[9]) + centre.x, (drawSphereVertecies[10]) + centre.y);
        LineTo(hdc, (drawSphereVertecies[0]) + centre.x, (drawSphereVertecies[1]) + centre.y);

        MoveToEx(hdc, (drawSphereVertecies[12]) + centre.x, (drawSphereVertecies[13]) + centre.y, NULL);
        LineTo(hdc, (drawSphereVertecies[15]) + centre.x, (drawSphereVertecies[16]) + centre.y);
        LineTo(hdc, (drawSphereVertecies[18]) + centre.x, (drawSphereVertecies[19]) + centre.y);
        LineTo(hdc, (drawSphereVertecies[21]) + centre.x, (drawSphereVertecies[22]) + centre.y);
        LineTo(hdc, (drawSphereVertecies[12]) + centre.x, (drawSphereVertecies[13]) + centre.y);

        MoveToEx(hdc, (drawSphereVertecies[0]) + centre.x, (drawSphereVertecies[1]) + centre.y, NULL);
        LineTo(hdc, (drawSphereVertecies[12]) + centre.x, (drawSphereVertecies[13]) + centre.y);
        MoveToEx(hdc, (drawSphereVertecies[3]) + centre.x, (drawSphereVertecies[4]) + centre.y, NULL);
        LineTo(hdc, (drawSphereVertecies[15]) + centre.x, (drawSphereVertecies[16]) + centre.y);
        MoveToEx(hdc, (drawSphereVertecies[6]) + centre.x, (drawSphereVertecies[7]) + centre.y, NULL);
        LineTo(hdc, (drawSphereVertecies[18]) + centre.x, (drawSphereVertecies[19]) + centre.y);
        MoveToEx(hdc, (drawSphereVertecies[9]) + centre.x, (drawSphereVertecies[10]) + centre.y, NULL);
        LineTo(hdc, (drawSphereVertecies[21]) + centre.x, (drawSphereVertecies[22]) + centre.y);
    }
    else
    {
        // just draw between each of the vertecies
        auto it = drawSphereVertecies.begin();

        double startx = *it++;
        double starty = *it++;
        double startz = *it++;
        it = drawSphereVertecies.begin();

        MoveToEx(hdc, static_cast<int>(startx + centre.x), static_cast<int>(starty + centre.y), NULL);
        int counter = 0;
        while (it != drawSphereVertecies.end())
        {
            int x = (int)*it++; counter++;
            int y = (int)*it++; counter++;
            int z = (int)*it++; counter++;

            if (UseCube)
            {
                LineTo(hdc, x + centre.x, y + centre.y);
            }

            if (z >= 0.0)
                SetPixelV(hdc, x + centre.x, y + centre.y, 0x0000FFFF);

            if (stride > 1)
            {
                it += (stride - 1) * 3;
            }
        }

    }

    DrawText(hdc, rateMsg, -1, &cliRect, DT_LEFT);

    if (useAPL)
    {
        LeaveCriticalSection(&cubeDraw[0]);
    }
    else
    {
        for (int x = 0; x < ThreadCount; x++)
        {
            LeaveCriticalSection(&cubeDraw[x]);
        }
    }

    SelectObject(hdc, oldfon);
    SelectObject(hdc, oldPen);
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
#if defined(_M_ARM64) && defined(_USE_ARMPL_DEFINES)
        case ID_OPTIONS_USE:
            useAPL = !useAPL;

            // Set the check
            CheckMenuItem(GetMenu(hwnd) , ID_OPTIONS_USE, (useAPL ? MF_CHECKED : 0));
            break;
#endif
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
        if (wParam == 1)
        {
			// Invalidate the window to trigger a WM_PAINT for redrawing
            InvalidateRect(hwnd, NULL, FALSE);
		}
        else
        {
            DWORD now = GetTickCount();
            DWORD diff = now - startTime;
            startTime = now;

            // calculate how much time is spent in the rotation
            auto c1 = 1000.0 * timeValBLAS / (double)Calculations;
            auto c2 = 1000.0 * timeVal / (double)Calculations;
            timeValBLAS = 0;
            timeVal = 0;

            int calcPerSecond = static_cast<int>((Calculations * 1000.0) / (diff * 1.0));
            Calculations = 0;
            std::wstringstream stream;
            stream << L"Transforms per second: " << std::fixed << std::setprecision(2) << calcPerSecond / 1000.0 << L"k (" << useAPL << ")";// BLAS: " << c1 << " and C : " << c2;
            wsprintf(rateMsg, stream.str().c_str());

        }
    }
    break;
    case WM_CREATE:
    {
        // Set a timer to update the animation
        startTime = GetTickCount();
        SetTimer(hwnd, 1, 100, NULL);
        SetTimer(hwnd, 2, 1000, NULL);
        thread_handles.push_back(CreateThread(NULL, 0, ThreadProc, (LPVOID)ThreadCount, 0, NULL));
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

    for (int x = 0; x < ThreadCount; x++)
    {
        InitializeCriticalSection(&cubeDraw[x]);
    }

    sphereVertices = generateSpherePoints(spherePoints);
    drawSphereVertecies = sphereVertices;

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
#ifdef _M_ARM64
        L"Spinning Geometry Demo: Arm64" ,
#else
        L"Spinning Geometry Demo: x64",
#endif
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024 , 960,
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

    for (int x = 0; x < ThreadCount; x++)
    {
        DeleteCriticalSection(&cubeDraw[x]);
    }

    return (UINT) Msg.wParam;
}
