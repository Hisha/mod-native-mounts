-- Complete native mount Collections page for build 12340.
local PREFIX, VERSION = "NMOUNTS", "1"
local CATALOG = NATIVE_MOUNT_CATALOG or {}
local ROWS, ROW_HEIGHT = 9, 54
local COLLECTIONS_ICON = "Interface\\Icons\\Ability_Mount_RidingHorse"
local buttons, ownedSpells, filteredCatalog = {}, {}, {}
local pendingOwnedSpells
local offset, ownershipReady, selectedSpell, selectedKey = 0, false, nil, nil
local currentFilter, randomMountSpell = "ALL", nil
local nextRequestId, activeRequestId = 0, 0
local randomButton, filterDropDown, collectionsIconArt, collectionsIconBorder
local raceIds = {Human=1,Orc=2,Dwarf=3,NightElf=4,Scourge=5,Tauren=6,Gnome=7,Troll=8,BloodElf=10,Draenei=11}

local function mountCompanionSlot(spellID)
 for slot = 1, GetNumCompanions("MOUNT") do
  local _, _, companionSpellID = GetCompanionInfo("MOUNT", slot)
  if companionSpellID == spellID then return slot end
 end
 return nil
end

local function spellBookSlot(spellID)
 if not spellID then return nil end
 for tab = 1, GetNumSpellTabs() do
  local _, _, first, count = GetSpellTabInfo(tab)
  for slot = first + 1, first + count do
   local kind, id = GetSpellBookItemInfo(slot, BOOKTYPE_SPELL)
   if kind == "SPELL" and id == spellID then return slot end
  end
 end
 return nil
end

local function maskHas(mask, id)
 return id and id > 0 and bit and bit.band(mask, bit.lshift(1, id - 1)) ~= 0
end

local function chooseVariant(entry)
 local _, raceToken = UnitRace("player"); local _, _, classId = UnitClass("player")
 local raceId = raceIds[raceToken]
 for _, variant in ipairs(entry.variants) do
  if maskHas(variant.races, raceId) and maskHas(variant.classes, classId) and
     mountCompanionSlot(variant.spell) then return variant end
 end
 for _, variant in ipairs(entry.variants) do
  if maskHas(variant.races, raceId) and maskHas(variant.classes, classId) then return variant end
 end
 for _, variant in ipairs(entry.variants) do
  if mountCompanionSlot(variant.spell) then return variant end
 end
 return entry.variants[1]
end

local function entryOwned(entry)
 for _, variant in ipairs(entry.variants) do if ownedSpells[variant.spell] then return true end end
 return false
end

local function clearPreview()
 selectedSpell, selectedKey = nil, nil
 NativeMountsPreview:ClearModel(); NativeMountsPreviewUnavailable:Show()
 NativeMountsSelectedName:SetText(""); NativeMountsRestriction:SetText("")
end

local function selectEntry(entry)
 if not entry then clearPreview(); return end
 local variant = chooseVariant(entry)
 selectedSpell, selectedKey = variant.spell, entry.key
 NativeMountsPreview:ClearModel()
 if variant.creature and variant.creature > 0 then
  NativeMountsPreview:SetCreature(variant.creature)
  NativeMountsPreview:SetFacing(0); NativeMountsPreview:SetModelScale(1)
  NativeMountsPreview:SetPosition(0, 0, 0); NativeMountsPreviewUnavailable:Hide()
 else NativeMountsPreview:ClearModel(); NativeMountsPreviewUnavailable:Show() end
 NativeMountsSelectedName:SetText(variant.name)
 if ownershipReady and entryOwned(entry) and not mountCompanionSlot(variant.spell) then
  NativeMountsRestriction:SetText("This account owns the mount, but this character does not meet its class, faction, profession, level, or riding requirement.")
 else NativeMountsRestriction:SetText("") end
end

local function refreshButton(button)
 local entry = button.mount
 if not entry then button:Hide(); return end
 button:Show()
 local variant = chooseVariant(entry); button.variant = variant
 local slot = mountCompanionSlot(variant.spell)
 button.companionSlot = slot
 local name, _, icon = GetSpellInfo(variant.spell)
 button.icon:SetTexture(icon or "Interface\\Icons\\INV_Misc_QuestionMark")
 button.name:SetText(name or variant.name)
 if slot then
  button.icon:SetDesaturated(false); button.status:SetText("Collected - drag to an action bar")
  button.status:SetTextColor(.2, 1, .2)
 elseif ownershipReady and entryOwned(entry) then
  button.icon:SetDesaturated(false); button.status:SetText("Collected - restricted on this character")
  button.status:SetTextColor(1, .72, .2)
 else
  button.icon:SetDesaturated(true)
  button.status:SetText(ownershipReady and "Uncollected" or "Loading account ownership...")
  button.status:SetTextColor(.65, .65, .65)
 end
end

local function updateCount()
 if not ownershipReady then
  NativeMountsCount:SetText(string.format("%d total  |  ownership loading...", #CATALOG)); return
 end
 local collected = 0
 for _, entry in ipairs(CATALOG) do if entryOwned(entry) then collected = collected + 1 end end
 NativeMountsCount:SetText(string.format("%d total  |  %d collected  |  %d uncollected",
  #CATALOG, collected, #CATALOG - collected))
end

local function refreshRows()
 for row, button in ipairs(buttons) do
  button.mount = filteredCatalog[offset + row]; refreshButton(button)
 end
 if NativeMountsScrollBar then
  NativeMountsScrollBar:SetMinMaxValues(0, math.max(0, #filteredCatalog - ROWS))
  NativeMountsScrollBar:SetValue(offset)
 end
 updateCount()
end

local function rebuildFiltered(resetScroll)
 filteredCatalog = {}
 for _, entry in ipairs(CATALOG) do
  local owned = ownershipReady and entryOwned(entry)
  if currentFilter == "ALL" or (ownershipReady and currentFilter == "COLLECTED" and owned) or
     (ownershipReady and currentFilter == "UNCOLLECTED" and not owned) then table.insert(filteredCatalog, entry) end
 end
 if resetScroll then offset = 0
 else offset = math.max(0, math.min(offset, math.max(0, #filteredCatalog - ROWS))) end
 if selectedKey then
  local selected
  for _, entry in ipairs(filteredCatalog) do if entry.key == selectedKey then selected = entry; break end end
  if selected then selectEntry(selected) else clearPreview() end
 end
 refreshRows()
end

local function setOffset(value)
 offset = math.max(0, math.min(math.floor(value + .5), math.max(0, #filteredCatalog - ROWS)))
 refreshRows()
end

local function setFilter(value)
 currentFilter = value
 if filterDropDown then
  UIDropDownMenu_SetSelectedValue(filterDropDown, value)
  UIDropDownMenu_SetText(filterDropDown,
   value == "COLLECTED" and "Collected" or value == "UNCOLLECTED" and "Uncollected" or "All")
 end
 rebuildFiltered(true)
end

local function configureRandomButton()
 if not randomButton then return end
 local name, _, icon = randomMountSpell and GetSpellInfo(randomMountSpell)
 randomButton.icon:SetTexture(icon or "Interface\\Icons\\Ability_Mount_RidingHorse")
 randomButton.spellName = name
 if name and spellBookSlot(randomMountSpell) then randomButton:Enable() else randomButton:Disable() end
 if not InCombatLockdown() then
  randomButton:SetAttribute("type", name and "spell" or nil); randomButton:SetAttribute("spell", name)
 end
end

local function setCollectionsIcon(mountsActive)
 local iconFrame = ContentManagerCollectionsFrame and ContentManagerCollectionsFrame.CollectionsIcon
 if not iconFrame then return end
 if not collectionsIconArt then
  for index = 1, select("#", iconFrame:GetRegions()) do
   local region = select(index, iconFrame:GetRegions())
   if region and region.SetTexture then collectionsIconArt = region; break end
  end
 end
 if not collectionsIconArt then return end
 if mountsActive then
  collectionsIconArt:ClearAllPoints(); collectionsIconArt:SetPoint("CENTER", iconFrame, "CENTER", 0, 0)
  collectionsIconArt:SetWidth(52); collectionsIconArt:SetHeight(52)
  collectionsIconArt:SetTexCoord(.07, .93, .07, .93); collectionsIconArt:SetTexture(COLLECTIONS_ICON)
  if not collectionsIconBorder then
   collectionsIconBorder = iconFrame:CreateTexture(nil, "OVERLAY")
   collectionsIconBorder:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")
   collectionsIconBorder:SetWidth(90); collectionsIconBorder:SetHeight(90)
   collectionsIconBorder:SetPoint("CENTER", iconFrame, "CENTER", 10, -10)
  end
  collectionsIconBorder:Show()
 else
  collectionsIconArt:ClearAllPoints(); collectionsIconArt:SetAllPoints(iconFrame)
  collectionsIconArt:SetTexCoord(0, 1, 0, 1)
  collectionsIconArt:SetTexture("Interface\\Buttons\\ContentManagerCollections")
  if collectionsIconBorder then collectionsIconBorder:Hide() end
 end
end

function NativeMounts_Row_OnDragStart(button)
 local slot = button.variant and mountCompanionSlot(button.variant.spell)
 if not slot then refreshButton(button); return end
 PickupCompanion("MOUNT", slot)
end

function NativeMounts_Row_OnClick(button) if button.mount then selectEntry(button.mount) end end

function NativeMounts_Row_OnEnter(button)
 if not button.mount then return end
 GameTooltip:SetOwner(button, "ANCHOR_RIGHT")
 GameTooltip:SetHyperlink("spell:" .. button.variant.spell); GameTooltip:Show()
end

local function requestOwnership()
 if SendAddonMessage and UnitName("player") then
  nextRequestId = nextRequestId + 1; activeRequestId = nextRequestId
  rebuildFiltered(false)
  SendAddonMessage(PREFIX, VERSION .. "\tSYNC\t" .. activeRequestId, "WHISPER", UnitName("player"))
 end
end

local function handleAddon(prefix, message, channel, sender)
 if prefix ~= PREFIX or channel ~= "WHISPER" or sender ~= UnitName("player") then return end
 local version, operation, request, body = message:match("^(%d+)\t([^\t]+)\t(%d+)\t?(.*)$")
 if version ~= VERSION or tonumber(request) ~= activeRequestId then return end
 if operation == "RESET" then pendingOwnedSpells = {}
 elseif operation == "OWN" then
  if not pendingOwnedSpells then pendingOwnedSpells = {} end
  for value in body:gmatch("%d+") do pendingOwnedSpells[tonumber(value)] = true end
 elseif operation == "ABILITY" then randomMountSpell = tonumber(body); configureRandomButton()
 elseif operation == "END" then
  ownedSpells = pendingOwnedSpells or {}; pendingOwnedSpells = nil
  ownershipReady = true; rebuildFiltered(false)
 end
end

function NativeMounts_OnLoad(frame)
 if RegisterAddonMessagePrefix then RegisterAddonMessagePrefix(PREFIX) end
 frame:RegisterEvent("SPELLS_CHANGED"); frame:RegisterEvent("COMPANION_LEARNED")
 frame:RegisterEvent("COMPANION_UNLEARNED"); frame:RegisterEvent("COMPANION_UPDATE")
 frame:RegisterEvent("CHAT_MSG_ADDON"); frame:RegisterEvent("PLAYER_REGEN_ENABLED")
 frame:SetScript("OnEvent", function(self, event, ...)
  if event == "CHAT_MSG_ADDON" then handleAddon(...)
  elseif event == "PLAYER_REGEN_ENABLED" or event == "SPELLS_CHANGED" then
   configureRandomButton(); if self:IsShown() then rebuildFiltered(false) end
  elseif self:IsShown() then rebuildFiltered(false) end
 end)
 frame:EnableMouseWheel(true)
 frame:SetScript("OnMouseWheel", function(_, delta) setOffset(offset - delta * 3) end)

 filterDropDown = CreateFrame("Frame", "NativeMountsFilterDropDown", frame, "UIDropDownMenuTemplate")
 filterDropDown:SetPoint("TOPLEFT", 225, 2); UIDropDownMenu_SetWidth(filterDropDown, 105)
 UIDropDownMenu_Initialize(filterDropDown, function()
  local function addOption(value, label)
   local info = UIDropDownMenu_CreateInfo(); info.text = label; info.value = value
   info.checked = currentFilter == value; info.func = function() setFilter(value) end
   UIDropDownMenu_AddButton(info)
  end
  for _, option in ipairs({{"ALL","All"},{"COLLECTED","Collected"},{"UNCOLLECTED","Uncollected"}}) do
   addOption(option[1], option[2])
  end
 end)
 UIDropDownMenu_SetSelectedValue(filterDropDown, "ALL"); UIDropDownMenu_SetText(filterDropDown, "All")

 for row = 1, ROWS do
  local button = CreateFrame("Button", "NativeMountsRow" .. row, frame)
  button:SetWidth(330); button:SetHeight(ROW_HEIGHT)
  button:SetPoint("TOPLEFT", 18, -42 - ((row - 1) * ROW_HEIGHT))
  button:RegisterForDrag("LeftButton"); button:SetScript("OnDragStart", NativeMounts_Row_OnDragStart)
  button:SetScript("OnClick", NativeMounts_Row_OnClick); button:SetScript("OnEnter", NativeMounts_Row_OnEnter)
  button:SetScript("OnLeave", function() GameTooltip:Hide() end)
  button.icon = button:CreateTexture(nil, "ARTWORK"); button.icon:SetWidth(42); button.icon:SetHeight(42)
  button.icon:SetPoint("LEFT", 4, 0)
  button.name = button:CreateFontString(nil, "ARTWORK", "GameFontNormal")
  button.name:SetPoint("TOPLEFT", button.icon, "TOPRIGHT", 9, -5)
  button.status = button:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
  button.status:SetPoint("TOPLEFT", button.name, "BOTTOMLEFT", 0, -6)
  button:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
  buttons[row] = button
 end
 local scroll = CreateFrame("Slider", "NativeMountsScrollBar", frame, "UIPanelScrollBarTemplate")
 scroll:SetPoint("TOPLEFT", 350, -58); scroll:SetPoint("BOTTOMLEFT", 350, 70)
 scroll:SetValueStep(1); scroll:SetScript("OnValueChanged", function(_, value) setOffset(value) end)

 randomButton = CreateFrame("Button", "NativeMountsRandomButton", frame, "SecureActionButtonTemplate")
 randomButton:SetWidth(44); randomButton:SetHeight(44); randomButton:SetPoint("BOTTOMRIGHT", -148, 92)
 randomButton:RegisterForClicks("LeftButtonUp"); randomButton:RegisterForDrag("LeftButton")
 randomButton.icon = randomButton:CreateTexture(nil, "ARTWORK"); randomButton.icon:SetAllPoints(randomButton)
 randomButton.border = randomButton:CreateTexture(nil, "OVERLAY"); randomButton.border:SetTexture("Interface\\Buttons\\UI-Quickslot2")
 randomButton.border:SetPoint("TOPLEFT", -1, 1); randomButton.border:SetPoint("BOTTOMRIGHT", 1, -1)
 randomButton:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
 randomButton:SetScript("OnDragStart", function()
  local slot = spellBookSlot(randomMountSpell); if slot then PickupSpell(slot, BOOKTYPE_SPELL) end
 end)
 randomButton:SetScript("OnEnter", function(self)
  GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
  if randomMountSpell then GameTooltip:SetHyperlink("spell:" .. randomMountSpell)
  else GameTooltip:SetText("Random Mount", 1, 1, 1); GameTooltip:AddLine("Available after ownership synchronization.", .7, .7, .7) end
  GameTooltip:Show()
 end)
 randomButton:SetScript("OnLeave", function() GameTooltip:Hide() end)
 randomButton.label = frame:CreateFontString(nil, "ARTWORK", "GameFontNormal")
 randomButton.label:SetPoint("LEFT", randomButton, "RIGHT", 8, 0); randomButton.label:SetText("Random Mount")
 configureRandomButton(); rebuildFiltered(false)
end

function NativeMounts_OpenCollections()
 setCollectionsIcon(true); NativeMountsCollectionsFrame:Show(); rebuildFiltered(false); requestOwnership()
 if not selectedKey and filteredCatalog[1] then selectEntry(filteredCatalog[1]) end
end

function NativeMounts_CloseCollections() setCollectionsIcon(false); NativeMountsCollectionsFrame:Hide() end
