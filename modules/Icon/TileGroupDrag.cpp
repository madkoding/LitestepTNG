#define STRICT_TYPED_ITEMIDS

//-------------------------------------------------------------------------------------------------
// /Icon/TileGroupDrag.cpp
// The nModules Project
//
// Dragging icons: out of the group (to a folder, a program, the Recycle Bin), into it
// (files dropped on the group go to its folder), and inside it, to put an icon in another
// slot. The order chosen that way is remembered across restarts.
//-------------------------------------------------------------------------------------------------
#include "TileGroup.hpp"

#include "../ModuleKit/LiteStep.h"
#include "../Utilities/BitmapAlpha.h"

#include <algorithm>
#include <strsafe.h>
#include <vector>


/// <summary>
/// The registry key the slots of this group's icons are remembered under: one value per
/// icon, named by the icon's parsing name, holding its slot number.
/// </summary>
static void OrderKey(LPCWSTR prefix, LPWSTR key, size_t cchKey) {
  StringCchPrintfW(key, cchKey, L"Software\\LiteStepTNG\\IconOrder\\%s", prefix);
}


// --- The group as a drop target --------------------------------------------------------

TileGroup::DropTarget::DropTarget(TileGroup *group) : mGroup(group), mForward(nullptr) {}

// The target lives and dies with its group; COM's reference counting is not used.
ULONG WINAPI TileGroup::DropTarget::AddRef() { return 2; }
ULONG WINAPI TileGroup::DropTarget::Release() { return 1; }

HRESULT WINAPI TileGroup::DropTarget::QueryInterface(REFIID riid, void **object) {
  if (object == nullptr) {
    return E_POINTER;
  }
  if (riid == IID_IUnknown || riid == IID_IDropTarget) {
    *object = static_cast<IDropTarget*>(this);
    return S_OK;
  }
  *object = nullptr;
  return E_NOINTERFACE;
}


void TileGroup::DropTarget::EndForward() {
  if (mForward != nullptr) {
    mForward->Release();
    mForward = nullptr;
  }
}


HRESULT WINAPI TileGroup::DropTarget::DragEnter(IDataObject *data, DWORD keys, POINTL point, DWORD *effect) {
  // One of our own icons being moved inside the group.
  if (mGroup->mDragOrigin != nullptr) {
    *effect &= DROPEFFECT_MOVE;
    return S_OK;
  }

  // Anything else is for the folder the group shows; the shell's own drop target for
  // that folder knows what to do with it (copy, move, shortcut, the right-drag menu).
  EndForward();
  if (mGroup->mWorkingFolder != nullptr && SUCCEEDED(mGroup->mWorkingFolder->CreateViewObject(
      mGroup->mWindow->GetWindowHandle(), IID_IDropTarget, reinterpret_cast<void**>(&mForward)))) {
    return mForward->DragEnter(data, keys, point, effect);
  }
  mForward = nullptr;
  *effect = DROPEFFECT_NONE;
  return S_OK;
}


HRESULT WINAPI TileGroup::DropTarget::DragOver(DWORD keys, POINTL point, DWORD *effect) {
  if (mGroup->mDragOrigin != nullptr) {
    *effect &= DROPEFFECT_MOVE;
    return S_OK;
  }
  if (mForward != nullptr) {
    return mForward->DragOver(keys, point, effect);
  }
  *effect = DROPEFFECT_NONE;
  return S_OK;
}


HRESULT WINAPI TileGroup::DropTarget::DragLeave() {
  if (mForward != nullptr) {
    mForward->DragLeave();
    EndForward();
  }
  return S_OK;
}


HRESULT WINAPI TileGroup::DropTarget::Drop(IDataObject *data, DWORD keys, POINTL point, DWORD *effect) {
  if (mGroup->mDragOrigin != nullptr) {
    // Only note where it was dropped; BeginDrag rearranges once the drag has ended.
    // The effect reported back is "nothing happened to the files", which is true, and
    // keeps the source from deleting anything in the name of a move.
    mGroup->mDroppedOnSelf = true;
    mGroup->mDropPoint.x = point.x;
    mGroup->mDropPoint.y = point.y;
    *effect = DROPEFFECT_NONE;
    return S_OK;
  }
  if (mForward != nullptr) {
    HRESULT hr = mForward->Drop(data, keys, point, effect);
    EndForward();
    return hr;
  }
  *effect = DROPEFFECT_NONE;
  return S_OK;
}


// --- Dragging -----------------------------------------------------------------------------

/// <summary>
/// Gives the drag a picture of what is being dragged: the item's preview or icon, held
/// by its centre. Without this the user only sees a cursor. The picture is stored in
/// the data object, which is where SHDoDragDrop and every drop target look for it.
/// </summary>
static void SetDragImage(IDataObject *data, IShellFolder2 *folder, PCITEMID_CHILD item, int size) {
  IShellItemImageFactory *factory = nullptr;
  if (FAILED(SHCreateItemWithParent(nullptr, folder, item, IID_PPV_ARGS(&factory)))) {
    return;
  }
  SIZE wanted = { size, size };
  HBITMAP bitmap = nullptr;
  HRESULT hr = factory->GetImage(wanted, SIIGBF_RESIZETOFIT, &bitmap);
  factory->Release();
  if (FAILED(hr) || bitmap == nullptr) {
    return;
  }
  BitmapAlpha::MakeOpaqueIfMissing(bitmap);

  BITMAP info;
  GetObjectW(bitmap, sizeof(info), &info);
  SHDRAGIMAGE image = {};
  image.sizeDragImage.cx = info.bmWidth;
  image.sizeDragImage.cy = info.bmHeight;
  image.ptOffset.x = info.bmWidth / 2;
  image.ptOffset.y = info.bmHeight / 2;
  image.hbmpDragImage = bitmap;
  image.crColorKey = CLR_NONE; // the bitmap has its own alpha channel

  IDragSourceHelper *helper = nullptr;
  hr = CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&helper));
  if (SUCCEEDED(hr)) {
    hr = helper->InitializeFromBitmap(&image, data);
    helper->Release();
  }
  // On success the helper owns the bitmap.
  if (FAILED(hr)) {
    DeleteObject(bitmap);
  }
}


/// <summary>
/// Called by a tile when the mouse has moved far enough with the button down. The drag
/// itself starts from a posted message, outside the tile's own message handling: it runs
/// a message loop, during which the tile may well be deleted (its file moved away).
/// </summary>
void TileGroup::RequestDrag(Tile *origin) {
  PostMessageW(mWindow->GetWindowHandle(), mDragMessage, 0, reinterpret_cast<LPARAM>(origin));
}


bool TileGroup::HasTile(Tile *tile) const {
  return std::find(mTiles.begin(), mTiles.end(), tile) != mTiles.end();
}


/// <summary>
/// Drags the selected icons (the one under the mouse joins the selection). Returns when
/// the user lets go.
/// </summary>
void TileGroup::BeginDrag(Tile *origin) {
  if (!HasTile(origin) || mWorkingFolder == nullptr) {
    return;
  }
  if (!origin->IsSelected()) {
    DeselectAll();
    origin->Select();
  }

  std::vector<PCITEMID_CHILD> items;
  for (Tile *tile : mTiles) {
    if (tile->IsSelected()) {
      items.push_back(tile->GetItem());
    }
  }

  IDataObject *data = nullptr;
  if (FAILED(mWorkingFolder->GetUIObjectOf(nullptr, UINT(items.size()), &items[0], IID_IDataObject,
      nullptr, reinterpret_cast<void**>(&data)))) {
    return;
  }

  SetDragImage(data, mWorkingFolder, origin->GetItem(), mTileSettings.mIconSize);

  mDragOrigin = origin;
  mDroppedOnSelf = false;
  DWORD effect = DROPEFFECT_NONE;
  // The shell supplies the drag image and the standard cursors.
  SHDoDragDrop(mWindow->GetWindowHandle(), data, nullptr,
    DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
  data->Release();
  mDragOrigin = nullptr;

  // Dropped on the group itself: put the icon in the slot under the mouse.
  if (mDroppedOnSelf && HasTile(origin)) {
    POINT point = mDropPoint;
    ScreenToClient(mWindow->GetWindowHandle(), &point);
    MoveTileToSlot(origin, SlotFromPoint(point));
  }
}


// --- Slots ----------------------------------------------------------------------------------

Tile *TileGroup::TileInSlot(int slot) const {
  for (Tile *tile : mTiles) {
    if (tile->GetPositionID() == slot) {
      return tile;
    }
  }
  return nullptr;
}


/// <summary>
/// The slot at a point of the group's window, or -1. Slots include half of the spacing
/// around them, so a drop between two icons still lands somewhere.
/// </summary>
int TileGroup::SlotFromPoint(POINT point) const {
  const int width = int(mWindow->GetSize().width + 0.5f);
  const int height = int(mWindow->GetSize().height + 0.5f);
  LayoutSettings layout = mLayoutSettings; // RectFromID is not const

  for (int slot = 0; slot < 2000; ++slot) {
    RECT rect = layout.RectFromID(slot, mTileWidth, mTileHeight, width, height);
    // Past the edge of the window: there are no more slots.
    if (rect.right <= 0 || rect.left >= width || rect.bottom <= 0 || rect.top >= height) {
      break;
    }
    InflateRect(&rect, (layout.mColumnSpacing + 1) / 2, (layout.mRowSpacing + 1) / 2);
    if (PtInRect(&rect, point)) {
      return slot;
    }
  }
  return -1;
}


/// <summary>
/// Puts a tile in a slot. If another tile is there, the two swap places.
/// </summary>
void TileGroup::MoveTileToSlot(Tile *tile, int slot) {
  const int from = tile->GetPositionID();
  if (slot < 0 || slot == from) {
    return;
  }
  const int width = int(mWindow->GetSize().width + 0.5f);
  const int height = int(mWindow->GetSize().height + 0.5f);

  if (Tile *other = TileInSlot(slot)) {
    RECT rect = mLayoutSettings.RectFromID(from, mTileWidth, mTileHeight, width, height);
    other->SetPosition(from, rect.left, rect.top);
    SaveSlot(other);
  }
  RECT rect = mLayoutSettings.RectFromID(slot, mTileWidth, mTileHeight, width, height);
  tile->SetPosition(slot, rect.left, rect.top);
  SaveSlot(tile);

  mWindow->Repaint();
}


void TileGroup::SaveSlot(Tile *tile) const {
  WCHAR key[MAX_RCCOMMAND + 64], name[MAX_PATH];
  OrderKey(mSettings->GetPrefix(), key, _countof(key));
  if (SUCCEEDED(tile->GetDisplayName(SHGDN_FORPARSING, name, _countof(name)))) {
    DWORD slot = DWORD(tile->GetPositionID());
    RegSetKeyValueW(HKEY_CURRENT_USER, key, name, REG_DWORD, &slot, sizeof(slot));
  }
}


/// <summary>
/// The slot the user gave this icon in an earlier session, or -1.
/// </summary>
int TileGroup::SavedSlot(PCITEMID_CHILD pidl) const {
  WCHAR key[MAX_RCCOMMAND + 64], name[MAX_PATH];
  OrderKey(mSettings->GetPrefix(), key, _countof(key));
  if (FAILED(GetDisplayNameOf(pidl, SHGDN_FORPARSING, name, _countof(name)))) {
    return -1;
  }
  DWORD slot = 0, size = sizeof(slot);
  if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, &slot, &size) != ERROR_SUCCESS) {
    return -1;
  }
  return int(slot);
}
