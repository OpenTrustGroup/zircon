// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// This zircon's fdio names the C types of the io protocol with an "io"
// prefix (ioDirectoryOpenRequest); fs as of April 2018 names them without
// one. The protocol is the same: same methods, same ordinals.

#include <fdio/io.fidl.h>

typedef ioDirectoryGetTokenRequest DirectoryGetTokenRequest;
typedef ioDirectoryGetTokenResponse DirectoryGetTokenResponse;
typedef ioDirectoryLinkRequest DirectoryLinkRequest;
typedef ioDirectoryLinkResponse DirectoryLinkResponse;
typedef ioDirectoryOpenRequest DirectoryOpenRequest;
typedef ioDirectoryReadDirentsRequest DirectoryReadDirentsRequest;
typedef ioDirectoryReadDirentsResponse DirectoryReadDirentsResponse;
typedef ioDirectoryRenameRequest DirectoryRenameRequest;
typedef ioDirectoryRenameResponse DirectoryRenameResponse;
typedef ioDirectoryRewindRequest DirectoryRewindRequest;
typedef ioDirectoryRewindResponse DirectoryRewindResponse;
typedef ioDirectoryUnlinkRequest DirectoryUnlinkRequest;
typedef ioDirectoryUnlinkResponse DirectoryUnlinkResponse;
typedef ioFileGetFlagsRequest FileGetFlagsRequest;
typedef ioFileGetFlagsResponse FileGetFlagsResponse;
typedef ioFileGetVmoAtRequest FileGetVmoAtRequest;
typedef ioFileGetVmoAtResponse FileGetVmoAtResponse;
typedef ioFileGetVmoRequest FileGetVmoRequest;
typedef ioFileGetVmoResponse FileGetVmoResponse;
typedef ioFileReadAtRequest FileReadAtRequest;
typedef ioFileReadAtResponse FileReadAtResponse;
typedef ioFileReadRequest FileReadRequest;
typedef ioFileReadResponse FileReadResponse;
typedef ioFileSeekRequest FileSeekRequest;
typedef ioFileSeekResponse FileSeekResponse;
typedef ioFileSetFlagsRequest FileSetFlagsRequest;
typedef ioFileSetFlagsResponse FileSetFlagsResponse;
typedef ioFileTruncateRequest FileTruncateRequest;
typedef ioFileTruncateResponse FileTruncateResponse;
typedef ioFileWriteAtRequest FileWriteAtRequest;
typedef ioFileWriteAtResponse FileWriteAtResponse;
typedef ioFileWriteRequest FileWriteRequest;
typedef ioFileWriteResponse FileWriteResponse;
typedef ioNodeGetAttrRequest NodeGetAttrRequest;
typedef ioNodeGetAttrResponse NodeGetAttrResponse;
typedef ioNodeIoctlRequest NodeIoctlRequest;
typedef ioNodeIoctlResponse NodeIoctlResponse;
typedef ioNodeSetAttrRequest NodeSetAttrRequest;
typedef ioNodeSetAttrResponse NodeSetAttrResponse;
typedef ioNodeSyncRequest NodeSyncRequest;
typedef ioNodeSyncResponse NodeSyncResponse;
typedef ioObjectBindRequest ObjectBindRequest;
typedef ioObjectCloneRequest ObjectCloneRequest;
typedef ioObjectCloseRequest ObjectCloseRequest;
typedef ioObjectCloseResponse ObjectCloseResponse;
typedef ioObjectDescribeRequest ObjectDescribeRequest;
typedef ioObjectDescribeResponse ObjectDescribeResponse;
typedef ioObjectListInterfacesRequest ObjectListInterfacesRequest;
typedef ioObjectListInterfacesResponse ObjectListInterfacesResponse;
typedef ioObjectOnOpenEvent ObjectOnOpenEvent;
