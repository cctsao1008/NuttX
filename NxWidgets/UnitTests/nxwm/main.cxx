/////////////////////////////////////////////////////////////////////////////
// NxWidgets/UnitTests/nxwm/main.cxx
//
//   Copyright (C) 2012 Gregory Nutt. All rights reserved.
//   Author: Gregory Nutt <gnutt@nuttx.org>
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in
//    the documentation and/or other materials provided with the
//    distribution.
// 3. Neither the name NuttX, NxWidgets, nor the names of its contributors
//    me be used to endorse or promote products derived from this software
//    without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
// OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
// AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
// Included Files
/////////////////////////////////////////////////////////////////////////////

#include <nuttx/config.h>
#include <nuttx/arch.h>

#include <cstdio>
#include <cstdlib>
#include <cunistd>

#include "ctaskbar.hxx"
#include "cstartwindow.hxx"
#include "cnxconsole.hxx"

/////////////////////////////////////////////////////////////////////////////
// Pre-processor Definitions
/////////////////////////////////////////////////////////////////////////////

// What is the entry point called?

#ifdef CONFIG_NSH_BUILTIN_APPS
#  define MAIN_NAME nxwm_main
#  define MAIN_STRING "nxwm_main: "
#else
#  define MAIN_NAME user_start
#  define MAIN_STRING "user_start: "
#endif

#ifdef CONFIG_HAVE_FILENAME
#  define showTestStepMemory(msg) \
     _showTestStepMemory((FAR const char*)__FILE__, (int)__LINE__, msg)
#  define showTestCaseMemory(msg) \
     _showTestCaseMemory((FAR const char*)__FILE__, (int)__LINE__, msg)
#  define showTestMemory(msg) \
     _showTestMemory((FAR const char*)__FILE__, (int)__LINE__, msg)
#endif

/////////////////////////////////////////////////////////////////////////////
// Private Types
/////////////////////////////////////////////////////////////////////////////

struct SNxWmTest
{
  NxWM::CTaskbar     *taskbar;     // The task bar
  NxWM::CStartWindow *startwindow; // The start window
  unsigned int        mmInitial;   // Initial memory usage
  unsigned int        mmStep;      // Memory Usage at beginning of test step
  unsigned int        mmSubStep;   // Memory Usage at beginning of test sub-step
};

/////////////////////////////////////////////////////////////////////////////
// Private Data
/////////////////////////////////////////////////////////////////////////////

static struct SNxWmTest g_nxwmtest;

/////////////////////////////////////////////////////////////////////////////
// Public Function Prototypes
/////////////////////////////////////////////////////////////////////////////

// Suppress name-mangling

extern "C" int MAIN_NAME(int argc, char *argv[]);

/////////////////////////////////////////////////////////////////////////////
// Public Functions
/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
// Name: updateMemoryUsage
/////////////////////////////////////////////////////////////////////////////

#ifdef CONFIG_HAVE_FILENAME
static void updateMemoryUsage(unsigned int *previous,
                              FAR const char *file, int line,
                              FAR const char *msg)
#else
static void updateMemoryUsage(unsigned int *previous,
                              FAR const char *msg)
#endif
{
  struct mallinfo mmcurrent;

  /* Get the current memory usage */

#ifdef CONFIG_CAN_PASS_STRUCTS
  mmcurrent = mallinfo();
#else
  (void)mallinfo(&mmcurrent);
#endif

  /* Show the change from the previous time */

#ifdef CONFIG_HAVE_FILENAME
  printf("File: %s Line: %d : %s\n", file, line, msg);
#else
  printf("\n%s:\n", msg);
#endif
  printf("  Before: %8u After: %8u Change: %8d\n",
         *previous, mmcurrent.uordblks, (int)mmcurrent.uordblks - (int)*previous);

  /* Set up for the next test */

  *previous =  mmcurrent.uordblks;
}

/////////////////////////////////////////////////////////////////////////////
// Name: showTestCaseMemory
/////////////////////////////////////////////////////////////////////////////

#ifdef CONFIG_HAVE_FILENAME
static void _showTestCaseMemory(FAR const char *file, int line, FAR const char *msg)
{
  updateMemoryUsage(&g_nxwmtest.mmStep, file, line, msg);
  g_nxwmtest.mmSubStep = g_nxwmtest.mmInitial;
}
#else
static void showTestCaseMemory(FAR const char *msg)
{
  updateMemoryUsage(&g_nxwmtest.mmStep, msg);
  g_nxwmtest.mmSubStep = g_nxwmtest.mmInitial;
}
#endif

/////////////////////////////////////////////////////////////////////////////
// Name: showTestMemory
/////////////////////////////////////////////////////////////////////////////

#ifdef CONFIG_HAVE_FILENAME
static void _showTestMemory(FAR const char *file, int line, FAR const char *msg)
{
  updateMemoryUsage(&g_nxwmtest.mmInitial, file, line, msg);
}
#else
static void showTestMemory(FAR const char *msg)
{
  updateMemoryUsage(&g_nxwmtest.mmInitial, msg);
}
#endif

/////////////////////////////////////////////////////////////////////////////
// Name: initMemoryUsage
/////////////////////////////////////////////////////////////////////////////

static void initMemoryUsage(void)
{
  struct mallinfo mmcurrent;

#ifdef CONFIG_CAN_PASS_STRUCTS
  mmcurrent = mallinfo();
#else
  (void)mallinfo(&mmcurrent);
#endif

  g_nxwmtest.mmInitial = mmcurrent.uordblks;
  g_nxwmtest.mmStep    = mmcurrent.uordblks;
  g_nxwmtest.mmSubStep = mmcurrent.uordblks;
}

/////////////////////////////////////////////////////////////////////////////
// Public Functions
/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
// Name: showTestStepMemory
/////////////////////////////////////////////////////////////////////////////
// Called by ad hoc instrumentation in the NxWM/NxWidgets code

#ifdef CONFIG_HAVE_FILENAME
void _showTestStepMemory(FAR const char *file, int line, FAR const char *msg)
{
  updateMemoryUsage(&g_nxwmtest.mmSubStep, file, line, msg);
}
#else
void showTestStepMemory(FAR const char *msg)
{
  updateMemoryUsage(&g_nxwmtest.mmSubStep, msg);
}
#endif

/////////////////////////////////////////////////////////////////////////////
// user_start/nxwm_main
/////////////////////////////////////////////////////////////////////////////

int MAIN_NAME(int argc, char *argv[])
{
  // Call all C++ static constructors

#if defined(CONFIG_HAVE_CXX) && defined(CONFIG_HAVE_CXXINITIALIZE)
  up_cxxinitialize();
#endif

  // Initialize memory monitor logic

  initMemoryUsage();

  // Create an instance of the Task Bar.
  //
  // The general sequence for initializing the task bar is:
  //
  // 1. Create the CTaskbar instance,
  // 2. Call the CTaskbar::connect() method to connect to the NX server (CTaskbar
  //    inherits the connect method from CNxServer),
  // 3. Call the CTaskbar::initWindowManager() method to initialize the task bar.
  // 3. Call CTaskBar::startApplication repeatedly to add applications to the task bar
  // 4. Call CTaskBar::startWindowManager to start the display with applications in place

  printf(MAIN_STRING "Create CTaskbar instance\n");
  g_nxwmtest.taskbar = new NxWM::CTaskbar();
  if (!g_nxwmtest.taskbar)
    {
      printf(MAIN_STRING "ERROR: Failed to instantiate CTaskbar\n");
      return EXIT_FAILURE;
    }
  showTestCaseMemory("After create taskbar");

  // Connect to the NX server

  printf(MAIN_STRING "Connect the CTaskbar instance to the NX server\n");
  if (!g_nxwmtest.taskbar->connect())
    {
      printf(MAIN_STRING "ERROR: Failed to connect the CTaskbar instance to the NX server\n");
      delete g_nxwmtest.taskbar;
      return EXIT_FAILURE;
    }
  showTestCaseMemory("After connecting to the server");

  // Initialize the task bar
  //
  // Taskbar::initWindowManager() prepares the task bar to receive applications.
  // CTaskBar::startWindowManager() brings the window manager up with those applications
  // in place.

  printf(MAIN_STRING "Initialize the CTaskbar instance\n");
  if (!g_nxwmtest.taskbar->initWindowManager())
    {
      printf(MAIN_STRING "ERROR: Failed to intialize the CTaskbar instance\n");
      delete g_nxwmtest.taskbar;
      return EXIT_FAILURE;
    }
    showTestCaseMemory("After initializing window manager");

  // Create the start window.  The general sequence for setting up the start window is:
  //
  // 1. Call CTaskBar::openApplicationWindow to create a window for the start window,
  // 2. Use the window to instantiate Cstartwindow
  // 3. Call Cstartwindow::addApplication numerous times to install applications
  //    in the start window.
  // 4. Call CTaskBar::startApplication (initially minimized) to start the start
  //    window application.

  printf(MAIN_STRING "Opening the start window application window\n");
  NxWM::CApplicationWindow *window = g_nxwmtest.taskbar->openApplicationWindow();
  if (!window)
    {
      printf(MAIN_STRING "ERROR: Failed to create CApplicationWindow for the start window\n");
      delete g_nxwmtest.taskbar;
      return EXIT_FAILURE;
    }
  showTestCaseMemory("After creating start window application window");

  printf(MAIN_STRING "Initialize the CApplicationWindow\n");
  if (!window->open())
    {
      printf(MAIN_STRING "ERROR: Failed to open the CApplicationWindow \n");
      delete window;
      delete g_nxwmtest.taskbar;
      return EXIT_FAILURE;
    }
  showTestCaseMemory("After initializing the start window application window");

  printf(MAIN_STRING "Creating the start window application\n");
  g_nxwmtest.startwindow = new NxWM::CStartWindow(g_nxwmtest.taskbar, window);
  if (!g_nxwmtest.startwindow)
    {
      printf(MAIN_STRING "ERROR: Failed to instantiate CStartWindow\n");
      delete window;
      delete g_nxwmtest.taskbar;
      return EXIT_FAILURE;
    }
  showTestCaseMemory("After create the start window application");

  // Initialize the NSH library

  printf(MAIN_STRING "Initialize the NSH library\n");
  if (!NxWM::nshlibInitialize())
    {
      printf(MAIN_STRING "ERROR: Failed to initialize the NSH library\n");
      delete window;
      delete g_nxwmtest.taskbar;
      return EXIT_FAILURE;
    }
  showTestCaseMemory("After initializing the NSH library");

  // Add the NxConsole application to the start window

  NxWM::CNxConsole *console = (NxWM::CNxConsole *)0; // Avoid compiler complaint

  printf(MAIN_STRING "Opening the NxConsole application window\n");
  window = g_nxwmtest.taskbar->openApplicationWindow();
  if (!window)
    {
      printf(MAIN_STRING "ERROR: Failed to create CApplicationWindow for the NxConsole\n");
      goto noconsole;
    }
  showTestCaseMemory("After creating the NxConsole application window");

  printf(MAIN_STRING "Initialize the CApplicationWindow\n");
  if (!window->open())
    {
      printf(MAIN_STRING "ERROR: Failed to open the CApplicationWindow \n");
      delete window;
      delete g_nxwmtest.taskbar;
      return EXIT_FAILURE;
    }
  showTestCaseMemory("After initializing the NxConsole application window");

  printf(MAIN_STRING "Creating the NxConsole application\n");
  console = new  NxWM::CNxConsole(g_nxwmtest.taskbar, window);
  if (!console)
    {
      printf(MAIN_STRING "ERROR: Failed to instantiate CNxConsole\n");
      delete window;
      goto noconsole;
    }
  showTestCaseMemory("After creating the NxConsole application");

  printf(MAIN_STRING "Adding the NxConsole application to the start window\n");
  if (!g_nxwmtest.startwindow->addApplication(console))
    {
      printf(MAIN_STRING "ERROR: Failed to add CNxConsole to the start window\n");
      delete window;
    }
  showTestCaseMemory("After adding the NxConsole application");

noconsole:

  // Add the calculator application to the start window

#if 0
  NxWM::CCalculator *calculator = (NxWM::CCalculator *)0; // Avoid compiler complaint

  printf(MAIN_STRING "Opening the calculator application window\n");
  window = g_nxwmtest.taskbar->openApplicationWindow();
  if (!window)
    {
      printf(MAIN_STRING "ERROR: Failed to create CApplicationWindow for the calculator\n");
      goto nocalculator;
    }
  showTestCaseMemory("After creating the calculator application window");

  printf(MAIN_STRING "Initialize the CApplicationWindow\n");
  if (!window->open())
    {
      printf(MAIN_STRING "ERROR: Failed to open the CApplicationWindow \n");
      delete window;
      delete g_nxwmtest.taskbar;
      return EXIT_FAILURE;
    }
  showTestCaseMemory("After creating the initializing application window");

  printf(MAIN_STRING "Creating the calculator application\n");
  calculator = new  NxWM::CCalculator(g_nxwmtest.taskbar, window);
  if (!calculator)
    {
      printf(MAIN_STRING "ERROR: Failed to instantiate calculator\n");
      delete window;
      goto nocalculator;
    }
  showTestCaseMemory("After creating the calculator application");

  printf(MAIN_STRING "Adding the calculator application to the start window\n");
  if (!g_nxwmtest.startwindow->addApplication(calculator))
    {
      printf(MAIN_STRING "ERROR: Failed to add calculator to the start window\n");
      delete window;
    }
  showTestCaseMemory("After adding the calculator application");

nocalculator:
#endif

  // Call CTaskBar::startApplication to start the start window application.  The initial
  // state of the start window is minimized.

  printf(MAIN_STRING "Start the start window application\n");
  if (!g_nxwmtest.taskbar->startApplication(g_nxwmtest.startwindow, true))
    {
      printf(MAIN_STRING "ERROR: Failed to start the start window application\n");

      // Delete the task bar then the start window.  the order is important because
      // we must bet all of the application references out of the task bar before
      // deleting the start window.  When the start window is deleted, it will
      // also delete of of the resouces contained within the start window.

      delete g_nxwmtest.taskbar;
      delete g_nxwmtest.startwindow;
      return EXIT_FAILURE;
    }
  showTestCaseMemory("After starting the start window application");

  // Call CTaskBar::startWindowManager to start the display with applications in place.
  // This method will not return but will enter the task bar's modal loop.

  printf(MAIN_STRING "Start the window manager\n");
  if (!g_nxwmtest.taskbar->startWindowManager())
    {
      printf(MAIN_STRING "ERROR: Failed to start the window manager\n");

      // Delete the task bar then the start window.  the order is important because
      // we must bet all of the application references out of the task bar before
      // deleting the start window.  When the start window is deleted, it will
      // also delete of of the resouces contained within the start window.

      delete g_nxwmtest.taskbar;
      delete g_nxwmtest.startwindow;
      return EXIT_FAILURE;
    }
  showTestCaseMemory("After starting the window manager");

  // Wait a little bit for the display to stabilize.  The simulation pressing of
  // the 'start window' icon in the task bar

  sleep(2);
  g_nxwmtest.taskbar->clickIcon(0);
  showTestCaseMemory("After clicking the start window icon");

  // Wait bit to see the result of the button press.  The press the first icon
  // in the start menu.  That should be the NxConsole icon.

  sleep(2);
  g_nxwmtest.startwindow->clickIcon(0);
  showTestCaseMemory("After clicking the NxConsole icon");
  
  // Wait bit to see the result of the button press.

  sleep(2);
  showTestMemory("Final memory usage");
  return EXIT_SUCCESS;    
}
