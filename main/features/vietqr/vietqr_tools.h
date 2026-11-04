#ifndef VIETQR_TOOLS_H
#define VIETQR_TOOLS_H

namespace vietqr {

// Register MCP tools: self.vietqr.set_account / show / get_account / clear_account.
// Account info is stored in NVS namespace "vietqr" (never in the source code).
void RegisterTools();

}  // namespace vietqr

#endif  // VIETQR_TOOLS_H
