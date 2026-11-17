#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace sdlviewer
{
enum class UiType
{
    UnknownUi = 0,
    Pannel,
    Text,
    ClickBtn,
    ToggleBtn,
    imgBtn,
    img,
    slide,
    group,
    graph,
    filelist,
    popup,
    VideoArea
};

enum class DockType
{
    None = 0,
    Top,
    Left,
    Right,
    Bottom,
    Fill
};

enum class ScaleType
{
    Fixed = 0,
    Expanding
};

enum class AlignType
{
    None = 0,
    Left,
    Center,
    Right
};

enum class LayoutType
{
    Auto = 0,
    Vertical,
    Horizontal
};

enum class BtnType
{
    Normal = 0,
    Toggle,
    DragType
};

struct Position
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Size
{
    float width = 0.0f;
    float height = 0.0f;
};

struct Margin
{
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

struct Scales
{
    ScaleType pos = ScaleType::Fixed;
    ScaleType width = ScaleType::Fixed;
    ScaleType height = ScaleType::Fixed;
};

struct Font
{
    std::string family;
    std::string weight;
    int size = 0;
};

struct StartData
{
    Size size;
    Font font;
    std::vector<std::string> linkFile;
};

struct Color
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

struct EventColor
{
    Color Normal;
    Color Select;
    Color Hover;
};

struct Eventimages
{
    std::string normalImg;
    std::string hoverImg;
    std::string clickImg;
};

struct WidgetConfig
{
    std::string objName;
    UiType type = UiType::UnknownUi;
    Position position;
    Size size;
    Margin margin;
    Scales scale;
    Font font;
    EventColor color;
    EventColor textColor;
    EventColor border;
    std::string image;
    std::vector<std::string> images;
    Eventimages eventimages;
    std::string text;
    BtnType btnType = BtnType::Normal;
    LayoutType LayoutMod = LayoutType::Auto;
    AlignType Align = AlignType::None;
    float inspace = 0.0f;
    DockType dock = DockType::None;
    int dockOrder = 0;
    std::vector<std::string> grouplist;
};

using ConfigResolver = std::function<WidgetConfig *(const std::string &)>;

NLOHMANN_JSON_SERIALIZE_ENUM(UiType, {
    {UiType::UnknownUi, nullptr},
    {UiType::Pannel, "Pannel"},
    {UiType::Pannel, "Panel"},
    {UiType::Text, "Text"},
    {UiType::ClickBtn, "ClickBtn"},
    {UiType::ToggleBtn, "ToggleBtn"},
    {UiType::imgBtn, "imgBtn"},
    {UiType::img, "img"},
    {UiType::slide, "slide"},
    {UiType::group, "group"},
    {UiType::graph, "graph"},
    {UiType::filelist, "filelist"},
    {UiType::popup, "popup"},
    {UiType::VideoArea, "VideoArea"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(DockType, {
    {DockType::None, nullptr},
    {DockType::None, "None"},
    {DockType::Top, "Top"},
    {DockType::Left, "Left"},
    {DockType::Right, "Right"},
    {DockType::Bottom, "Bottom"},
    {DockType::Fill, "Fill"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(AlignType, {
    {AlignType::None, nullptr},
    {AlignType::None, "None"},
    {AlignType::Left, "Left"},
    {AlignType::Center, "Center"},
    {AlignType::Right, "Right"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(LayoutType, {
    {LayoutType::Auto, nullptr},
    {LayoutType::Auto, "Auto"},
    {LayoutType::Vertical, "Vertical"},
    {LayoutType::Horizontal, "Horizontal"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(ScaleType, {
    {ScaleType::Fixed, nullptr},
    {ScaleType::Fixed, "Fixed"},
    {ScaleType::Expanding, "Expanding"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(BtnType, {
    {BtnType::Normal, nullptr},
    {BtnType::Normal, "Normal"},
    {BtnType::Toggle, "Toggle"},
    {BtnType::DragType, "DragType"}
})

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Position, x, y)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Size, width, height)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Margin, left, top, right, bottom)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scales, pos, width, height)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Font, family, weight, size)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Color, r, g, b, a)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Eventimages, normalImg, hoverImg, clickImg)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EventColor, Normal, Select, Hover)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    WidgetConfig,
    objName,
    type,
    position,
    size,
    margin,
    scale,
    font,
    color,
    textColor,
    border,
    image,
    images,
    eventimages,
    text,
    btnType,
    LayoutMod,
    Align,
    inspace,
    dock,
    dockOrder,
    grouplist
)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StartData, size, font, linkFile)
} // namespace sdlviewer
