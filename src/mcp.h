#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

// A Model Context Protocol server over stdio, so AI agents (Claude Code,
// Codex, OpenCode, ...) can list, create, start, stop, share, and remove
// databases with the same rules as the window. One JSON-RPC message per line
// on stdin, one per line on stdout; nothing else is written to stdout.
namespace mcp {

// The protocol versions this server speaks, newest first.
QStringList supportedVersions();

// The tools as tools/list returns them.
QJsonArray tools();

// Answers one JSON-RPC message. Returns an empty value for notifications,
// which get no reply.
QJsonValue handle(const QJsonObject &message);

// Reads stdin until it closes. Returns the exit code.
int serve();

}  // namespace mcp
