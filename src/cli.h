#pragma once

#include <QStringList>

class Keyboard;

// Commands that need no window, for scripts, hooks, keybindings, and agents.
// Each exits 0 on success and 1 with the reason on stderr.
namespace Cli {

int run(const QStringList &arguments, Keyboard *keyboard, const QString &statePath, const QString &profilesPath);

} // namespace Cli
