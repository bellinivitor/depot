#pragma once

#include <QStringList>

// Commands that need no display, for scripts and agents. Returns the exit
// code, or -1 when the arguments ask for the window instead.
int runCli(const QStringList &arguments);
