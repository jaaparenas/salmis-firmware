#ifndef MEDIA_MCP_TOOLS_H_
#define MEDIA_MCP_TOOLS_H_

// Registers the self.media.* MCP tools (list, play, stop, status, rescan).
// Call from a board's constructor once the microSD is mounted.
void RegisterMediaMcpTools();

#endif  // MEDIA_MCP_TOOLS_H_
