# Content and classes (design)

Status: stage 1 is built except spawning (which waits for avatars, stage 6): class assets, the game's declared
classes, the mod metadata with dependencies and the minimum game version, the dependency rule, `rex.content` and the
editors setting. Stage 2 is built for the game's types and classes: classes standing for game types, typed object
variables and params, type checked links, and casts. The content browser and the properties editor (stages 7 and 8)
are built ahead of stages 3 to 6, so what those add lands in an editor that already exists. Stage 3 is built: mods'
enums and structs as assets with their editors, the game's enums and structs, and arrays and maps on every property,
field, graph variable and param. Stage 4 is built: mods' data tables with their grid editor, the game's read only
tables, the table nodes and `rex.content`'s table functions. Stage 5 is built but for adding languages: mods' own texts
with string tables, the translation editor and translated texts at runtime, and language assets changing the game's
own texts in the languages it has. Stage 6 is built: the game's backings and components, avatar classes with a
backing and a component tree, spawning, ticking and removing their avatars from Lua and graphs, and the class editor's
Avatar section. Map edits are built: an asset changing one of the game's worlds each time it's entered, so far by
spawning avatars in it, edited in the world itself with markers and gizmos over the game's picture. Materials are
built: an asset of a game's material type, previewed and edited live with the game's renderer. Textures, the first of stage 11, are built: pictures imported in the content browser, which a game
can make its own textures of. Stage 9's viewport is built: an avatar class's editor shows the class drawn by the game,
its components over it with gizmos to move, turn and scale them. The rest is design.

RexFabric today gives mods code: hooks, Lua scripts, native plugins and node graphs. This design adds content: things a
mod defines as data, browses in a content browser and edits in the game, the way an engine editor works. A mod can
define new kinds of things (classes), set their defaults, and for things that live in the world, give them components
and a graph.

RexFabric owns the generic parts: the browser, the class system, the property and component model, the editors and the
file format. Each game decides what its world objects are and which components they can have, through its game API, so
the same editor serves every game RexFabric is attached to.

## Terms

| Term | Meaning |
|---|---|
| Asset | One file in a mod's content folder: a class, a texture, a model, a data table |
| Class | A kind of thing, defined by a parent class and the defaults it sets or adds. Every class comes from `Object` |
| Object | The root class. It has one property, the thumbnail the content browser shows |
| Property | A named, typed value a class declares; its value in a class is that class's default |
| Class defaults | The property values a class sets: its own new properties and the ones it overrides from its parent |
| Avatar | The class a game registers for things placed in its world. In Trouble in Paradise this matches the game's own `avatarInst_s` |
| Component | A part of an avatar with its own transform and properties, of a type the game defines (an fx emitter, a light) |
| Editor | A window that opens an asset. Which one opens depends on the asset's class |

## The content browser

The browser shows content by mod. Its root lists every mod, each with the mod's `icon.png` (the icon the mods menu
already shows), or a folder icon when the mod has none.

Opening a mod opens its `content` folder as if it were the mod itself: the browser's root for the mod is
`mods/<Mod>/content/`, and the `content` folder is never shown. The mod's other files (`mod.json`, `icon.png`,
`scripts`, native plugins) stay out of the way, so the workspace holds only what was made in the browser. Inside it the
folders are the user's: any layout they make works, and nothing in RexFabric expects a folder by name.

From the browser one can:

* create an asset of any class, or a subclass of a chosen class,
* rename, move, duplicate and delete assets and folders,
* open an asset in its editor,
* see what references an asset before deleting or renaming it, and fix the references on a rename,
* filter by class and search by name.

Disabled mods are listed too, dimmed, so their content can still be edited. Their classes are not loaded into the game
until the mod is enabled.

### What is built

The browser is `src/ui/content_browser.cpp`, over `src/content/assets.cpp`, which reads and changes the mods' content
folders as files. It opens with `rexfabric::ShowContentBrowser`, the game's `Config::content_browser_key` (rebindable
as `ui.bind.content_browser`), the Content Browser button of the mods menu, or the Content button of the node editor.
Like the node editor, it is hidden while `rexfabric.show_editors` is off.

| Part | What it does |
|---|---|
| Tree | Every mod, an off one dimmed, with the folders of its content below it. Opens down to the folder shown |
| Path | All Mods, the mod, then each folder, above the grid; each part goes there |
| Grid | The folder's content as tiles: at a mod's root its metadata first, in red, then folders, then assets. At All Mods, the mods with their icons |
| Tiles | A class is a cube, blue when its objects are of a game type (below `TiP/Avatar`, say), teal for a data class. Its second line is its parent; its tooltip is its whole chain. A class or a game asset with a thumbnail shows it in place of its icon (Thumbnails, below) |
| Search and class filter | List every asset below the folder shown (every mod's, from All Mods) whose name has the text and that is of the class or below it |
| Add, or a right click on empty space | One menu for both (`DrawNewItems`): New Folder; then submenus by category, Basic (New Class, named with its parent picked from the classes the mod may use, New Enum, New Struct, New Data Table, New Language), Advanced (New Map Edit, New World, New Material, where the game has worlds and material types), each category the game's asset types name (Game asset types), and Other for the types naming none; then Import Image and Import Mesh. Add also has New Mod (its folder, `mod.toml` and `content`, turned on at the end of the load order) |
| A tile's right click | Open, New Subclass, Rename, Duplicate, Copy Path, Delete |
| Dragging a tile | Onto a folder in the grid, the tree or the path: moves it there, within its own mod |
| Keys | Enter opens the tile picked, Delete asks to delete it |

Names are checked as they are typed, with the reason when one can't be used (Names, below). A rename or a move takes
what names it along: every class asset of every mod naming it (as a parent, a property's class, an asset property's
value or default) names the new path, and so does anything naming something inside a folder that moved. Files beside an
asset named after it (`Rock.graph.json`) go with it. A delete lists what names what is going first, and leaves those as
they are: they don't load until they're fixed, and their editors say why.

Every change is written straight to the files, and the classes load again (`content::LoadModClasses`), so graphs and Lua
see a new class as soon as it's saved. An open editor of an asset a rename changes reads its file again; one with
unsaved changes stops the rename until it's saved or closed. Closing the browser closes its editors, asking first
about unsaved changes.

Not yet: renaming a class doesn't rename it in graphs that use it as a type, moving between mods, and more than one
tile picked at once.

### Thumbnails

A tile shows a picture of its asset where it has one, as Unreal's content browser does, and its kind's icon where it
has none. Classes and game assets have thumbnails (`src/ui/thumbnails.cpp`); a texture's tile shows its own picture,
as it always has. A tile shows the first of these its asset has:

| | Where it comes from | Which assets |
|---|---|---|
| 1 | A picture of its own: a Texture asset, set by hand. A class's is its `thumbnail` property (Object's, so every class has it, and a subclass inherits its parent's); a game asset's is its `thumbnail` field, which RexFabric adds to every game asset type, shown as Thumbnail last in its field table and left out of its file while it's empty | Any class or game asset |
| 2 | Its type's 2D preview (`AssetTypeDecl::preview`), drawn from its layers in the tile as its editor draws them: nothing is rendered | A game asset of a type with a preview (a star leaf) |
| 3 | A picture the game's viewport renderer makes of its viewport (`ViewportRenderer::capture`), framed as its Capture Thumbnail framing says, or the default way: the camera Reset View gives, and for an asset viewport a second of play first, so an effect's particles are out | An avatar class (one with a backing or components, as the Avatar editor has a viewport for), and a game asset of a type with a `viewport` (an FX Emitter) |
| 4 | Its icon, as before; also while its picture is being made | The rest |

The made pictures (3) are kept outside the mods, so a mod never ships them and they never go in a repository: under
`thumbnails` beside the executable, as `thumbnails/<mod>/<path in the mod>.png`, 256 pixels square, with
`thumbnails/<mod>/index.json` saying what each was made from:

```
thumbnails/MyMod/Garden/Rock.png
thumbnails/MyMod/index.json
{"thumbnails": {"garden/rock": "9c41e0d2b7a35f18"}, "empty": {}}
```

The hash is of everything making the picture look as it does: the asset's file (its framing in it too), for a class
each mod ancestor's file, and the picture files of the Texture assets those files name (by when they were written and
how large they are). A thumbnail whose hash changed (a save, a refresh, a changed texture) is made again; the old one
shows until the new one is there. A picture that came back with nothing in it (an avatar class whose backing shows
nothing of the game's) is noted under `empty`, and its tile keeps the icon until its hash changes. Deleting the
`thumbnails` folder makes them all again.

They're made lazily, only for tiles scrolled into view (and the editors showing them), in the order they're asked for
and one at a time: the next is asked for once the last came back, and a tile scrolled away before its turn is left
out. The renderer has 30 seconds for each. A made picture not shown for a while is let go, and read again from its
PNG when it's next shown.

#### Capture Thumbnail

An avatar class's viewport and an asset viewport have Capture Thumbnail on their toolbar: it keeps the camera as it is
now (yaw, pitch, zoom), and for an asset viewport how long it has played, as the asset's thumbnail framing, so the
picture made of it looks the same on anyone's machine. Clear goes back to the default framing. It's an edit as any
other (undone, redone, saved with the rest), and the tile shows the new picture once the asset is saved and its
picture made again. The framing is a key at the top level of the asset's own file, beside its other keys, in a class
file and a game asset's alike, never a property (`src/content/thumbnail_view.h`):

```
"thumbnail_view": {"yaw": 40.0, "pitch": 18.0, "zoom": 1.6, "time": 1.25}
```

A class's editor shows the thumbnail its class gets under its Help, and a game asset's editor under its Thumbnail field,
both as last saved.

#### Making a picture

A game makes the pictures through its viewport renderer (The viewport): `capture` is optional, and without it every
tile that would get one keeps its icon.

```cpp
rexfabric::content::SetViewportRenderer({.show = Show, .close = Close, .capture = Capture});

void Capture(uint64_t viewport, const ViewportView& view, double seconds, std::function<void(ViewportCapture)> done);
```

| Part | What it is |
|---|---|
| `viewport` | A number of its own for the picture, as a viewport's: `close` with it means it's no longer wanted (the browser closed), and a `done` after that is ignored |
| `view` | What to draw, as `show` is asked, 256 by 256: a class's view as its editor gives it, or an asset viewport's, with the framing's camera, `playing` on and `replays` 0 |
| `seconds` | How long its time runs on the stage's clock before the picture is taken: the framing's time, else 0 for a class (as soon as its avatar shows) and 1 for an asset |
| `done` | Called once, on any thread, a few frames later, with a `ViewportCapture`: RGBA8 pixels, rows top first, tightly packed, their alpha straight, what the stage left clear see through (the tile's card shows behind it); or no pixels and a `message` saying why. Never waited for: the call returns at once and the game draws on its own time |

RexFabric asks for one picture at a time and writes it to the cache as it comes back. Trouble in Paradise draws each on
a stage of its own and reads it back from its native frame (DeTiP's docs/modding.md, The avatar viewport).

## The mod's metadata

At the top of every mod's root in the browser sits one asset of its own kind, the mod's metadata, shown in red so it
stands apart from the content. It holds everything about the mod in one place, edited in the properties editor:

| Property | Meaning |
|---|---|
| Name | The mod's name as players see it |
| Description | What it does |
| Version | The mod's own version |
| Author | Who made it |
| Dependencies | The mods it needs, each with the lowest version it works with |
| Minimum game version | The lowest version of the game (for DeTiP, of the build) it works with |
| Icon | Set with a button that picks an image and makes it the mod's `icon.png` |

It is not a new file: it is the browser's view of the mod's `mod.toml` in the technical root (the file Lua mods already
use for their name, description, version and author), extended with the fields above. Saving it writes `mod.toml`, and
a mod made outside the browser shows its existing `mod.toml` the same way. A mod without one gets one the first time
its metadata is saved. The metadata can't be deleted, renamed or moved; it belongs to the mod.

A mod whose dependencies are missing, too old, or whose minimum game version is above the running game's, is not loaded,
and the mods menu says why.

What is built: the metadata editor shows whether the mod loads and why not, and edits its name, version, author,
description, minimum game version and dependencies (each with the lowest version, picked from the installed mods). It
lists problems as they arise: a dependency not installed, off, or older than asked; a game version above the running
one; a folder name that can't name content. Saving rewrites `mod.toml` with toml++, keeping keys it doesn't know but
not comments. The icon shows, but is still set by putting `icon.png` in the mod's folder.

In `mod.toml`:

```toml
name = "Content Child"
version = "1.0"
min_game_version = "1.0"

[[dependencies]]
mod = "ContentBase"      # the mod's folder
version = "1.0"          # the lowest version it works with; leave out for any

# or, for dependencies without a version:
# dependencies = ["ContentBase", "RockPack"]
```

`min_game_version` is compared with the version the game gives RexFabric (`Config::game_version`); a game that gives
none skips the check. Versions compare part by part, numbers as numbers, so `1.10` is above `1.9`.

When RexFabric starts, the enabled mods whose requirements are met load, each after the mods it depends on and
otherwise in load order. The others are logged with the reason ("needs the mod NotThere, which isn't installed and
enabled"), as is a mod whose dependency can't load and a set of mods that depend on each other in a circle.

### Who can change mods

The editors ship in every build. What keeps players from changing what their mods do is one setting,
`rexfabric.show_editors`: off, every button and key that opens an editor is hidden (the node editor's button in the
mods menu and the Guide, its hotkey, `ShowNodeEditor`; the content browser later), and mods run as they are. On, as it
is by default, anyone can open the editors. It is a choice for the player, not a lock: someone who wants to look
inside a mod turns it on.

### Dependencies and references

A mod can use what its dependencies define, and only that. With a dependency on `RockPack`, a mod's classes can derive
from `RockPack/...` classes, its properties can reference `RockPack` assets, its graphs can use `RockPack` classes as
types and call their graphs' functions, and its Lua can `require` `RockPack`'s scripts. A reference to a mod that is not
a dependency is refused, in the editor with an offer to add the dependency, and in Lua with an error naming the missing
dependency. Dependencies load first, so whatever a mod uses is there before it loads.

## Names

An asset's name is its file name, its path in the browser and its id in the game, all the same string. So names are
restricted to what works as all three:

* letters, digits and `_` only, starting with a letter: `Decor_Rock_1` is a name, `Decoŕ Rock 1!` is not,
* at most 64 characters,
* unique within its folder, ignoring case, since Windows paths ignore case.

The browser refuses any other name as it is typed, with the reason. A name says what an asset is called, not what the
player sees: what the game shows (a display name, a description) is a property, free to have spaces and any language.

An asset is referred to by its full path as the browser shows it: the mod's folder name, then the folders inside its
content and the name, `MyDecorMod/Garden/Decor_Rock_1` for `mods/MyDecorMod/content/Garden/Decor_Rock_1.json`. The
`content` folder is left out of paths as it is out of the browser. A reference to another mod's asset makes that mod a
dependency. Folder names follow the same rules as asset names, so every path works on disk and in the game.

## Classes

Every class comes from `Object`. A class is either built in or an asset:

| Where it comes from | Examples |
|---|---|
| RexFabric | `Object`, and later asset classes such as `Texture` and `Model` |
| The game, through its game API | `Avatar` and the game's own kinds of avatar (a decor, a pinata) |
| A mod, as an asset | `Decor_Rock_1`, a subclass of a game class or of another mod's class |

A class asset names its parent and stores only what differs from it: the properties it adds, and the defaults it
changes. A default left alone follows the parent, so changing the parent changes every child that didn't override it.
Each property in an editor can be reset to its parent's value.

`Object` itself has a single property:

| Property | Type | Meaning |
|---|---|---|
| `thumbnail` | texture reference | A picture the content browser shows for the asset in place of the one it makes (an avatar class's) or of its icon; none for that one |

Thumbnails are made automatically where an asset has a viewport, and this picture, when set, is shown in their place
(The content browser, Thumbnails).

### Data classes

A subclass of `Object` that only adds properties is a data class: a set of named settings and nothing else. A mod can
make one to hold its configuration, a table of prices, or the settings a group of its avatars share, and other assets
reference it. A data class opens in the properties editor, a plain list of its properties, with no viewport or graph.

### Property types

| Type | Value |
|---|---|
| `bool`, `int`, `float`, `string` | as named; numbers may declare a range |
| `text` | a string the player sees, translatable |
| `enum` | one of a declared list, or of an enum the game declares |
| `vector`, `rotation`, `colour` | three or four numbers |
| `asset` | a reference to an asset of a given class (a texture, a data class, another avatar class). Its picker offers the textures for an asset of `Texture`, the materials for `Material`, the map edits for `MapEdit`, the meshes for `Mesh`, the mods' worlds for `World`, the game's and the mods' assets of a game asset type for its class (Game asset types), and the classes for any other class: what the mod may use, its own and its dependencies' |
| `string` from a table | a string that's one of a data table's row names, picked from them in the editor or typed: a game's table of what it has (Trouble in Paradise's `TiP/Effects`, its effects), so a property names one of the game's things by picking it rather than spelling it. The type's `of` names the table |
| a colour | a `string` holding `#RRGGBB`: the editor shows a swatch beside it that opens a colour picker. Any string written so gets one; a game's colour properties take this form |
| `tag` | a game identifier, for games that have them (in Trouble in Paradise, a tag such as a species or an item) |
| struct, enum | a struct or enum a mod made in the browser, or one the game declares (see below) |
| class | an instance of a class, or a class itself (see Types in graphs and variables) |

The game API can add types of its own the same way it adds functions today.

### Containers

Any property or graph variable can be a single value, an array or a map, switched with a button beside its type as in
Unreal:

| Container | Holds |
|---|---|
| Single | one value of the type |
| Array | an ordered list of values of the type |
| Map | values of the type, each under a key of a second type |

A map's key can be a type whose values compare exactly: `bool`, `int`, `string`, an enum, a `tag`, an asset reference
or a class. Floats, vectors, structs and the like can't be keys. Arrays and maps of structs work, so a property can be
a list of `RockSettings` or a map from a species tag to a `SpeciesTweak`.

### Structs and enums

A mod can make its own structs and enums in the browser, as assets of their own kind:

* A struct is a named set of typed fields, each with a default, its own type any property type, container or not. It
  opens in a field editor: add, remove, reorder, rename and retype fields.
* An enum is a named list of values. It opens in a list editor: add, remove, reorder and rename values, each with a
  display name the player may see.

Both are types as soon as they are saved, for their mod and the mods that depend on it, everywhere a type goes:
properties, graph variables, pins, map keys (enums) and data table rows (structs). Renaming a field or value updates the
assets that use it; removing one marks them, as removing a class does.

The game can declare its own real structs and enums through its API, with the game's field layout behind them, so mods
work with the game's data in the game's terms. For Trouble in Paradise that would be, for example, the requirement
kinds (`tip.Requirement` today), the tag classes, and a struct for a shop item's row. Game structs and enums can't be
edited in the browser; they show as read only, and a mod uses them like its own.

### What is built

The values are `src/content/values.cpp`: one checker (`FitValue`) and one walker (`WalkValues`) for every type, nested
or not, which the loaders, the editors, Lua and the graph compiler all use. The registry of enums and structs is
`src/content/types.cpp`; enums and structs load before classes, as classes' properties use them.

| Part | What it does |
|---|---|
| Containers | Any property, struct field, graph variable or param holds one value, an array or a map. A map's key is a bool, an int, a string, an enum or an asset |
| Mod enums | Assets of their own: each value a name (as values and Lua name it), a display name and help |
| Mod structs | Assets of their own: each field a name, a type (container or not, a struct of another struct too), a default and help. A field missing from a value is its default |
| The game's enums | The ones it declared through its API (`Namespace::Enum`), by their Lua names (`tip.Weather`). A value is kept by its name and reaches Lua and graphs as the game's number |
| The game's structs | `rexfabric::content::DeclareStruct`, read only, used like a mod's |
| Enum editor | Add, remove, reorder and rename values, each with a display name and help |
| Struct editor | Add, remove, reorder, rename and retype fields, each with its default |
| Type picker | One dropdown per property or field: what it holds (one, an array, a map and its key), then a plain type, an enum, a struct or an asset of a class |
| Values | Arrays and maps open as a tree, a row per element or entry; structs as a tree of their fields; enums as a list of their display names |

Renaming an enum's values or a struct's fields and saving renames them in every value of every class and struct asset
of every mod: defaults, elements, map keys and values, fields of structs inside structs. Removing one leaves the values
holding it to be fixed, each reported in its editor and its load. A struct can't hold itself, however deep; the struct
editor says which field does, and the loader skips it.

Not yet: renaming a value or a field doesn't reach graphs' starting values, the browser doesn't show the game's enums
and structs as read only assets of their own (they appear in every picker), and nothing yet has the game's own data
behind a struct: the game's structs are values, like a mod's.

## Types in graphs and variables

Every class is also a type. Graph variables, node pins, class properties and function arguments are typed, and a class
type can be as broad as `Avatar` or as narrow as one class:

| Type | Holds |
|---|---|
| `Avatar` | any avatar in the world |
| `TiP/PinataActor` | only pinatas |
| `ExampleMod/RandyModel/Randy` | only instances of that mod class or its subclasses |
| Class of `Avatar` | a class itself, not an instance: what a spawn node takes |

The types come from two places. The game's API registers the fundamental ones and their places in the hierarchy (for
Trouble in Paradise, `Avatar`, then pinatas, decor and the rest under it), as it declares its other types today. Every
class a mod makes in the content browser is a type too, as soon as it is saved, for its own mod and for every mod that
depends on it.

Types keep graphs safe:

* A link can only join pins whose types fit: a `TiP/PinataActor` output goes into an `Avatar` input, as every pinata
  is an avatar, but not the other way round.
* Going down the hierarchy takes a cast node, which has a success and a failure path, so a graph never treats an avatar
  as a pinata without checking.
* A game function's node takes the types the game declared, so a pinata only function can't be given a rock.
* Renaming or moving a class updates the types that name it; deleting one marks the pins and variables that used it,
  and the graph won't compile until they are fixed.

Compiled to Lua, typed values become checked handles: a value crossing into a typed function or a cast is checked
against its class, and a wrong one is an error naming both types. Lua written by hand gets the same checks through the
same functions.

### What is built

A game type and its class are one type. The game declares a class that stands for each of its types
(`TiP/Avatar` for `tip.Avatar`), and the hierarchy of those classes is the hierarchy of the types, so the game API's
types need no hierarchy of their own. A graph names a type the way Lua does: a game type by its Lua name
(`tip.Avatar`), any other class by its path (`TiP/Pinata`, `ExampleMod/RandyModel/Randy`).

| Part | Where | What it does |
|---|---|---|
| Object variables and params | the details panel | A variable, a function's or macro's input or output, or a dispatcher's param can be an Object, then picks its type: any object, a game type, or a class the mod may use (its own, its dependencies', the game's) |
| Type checked links | `graph::Fits` | An object goes into an input of its own type or of one above it (`content::TypeIsA`); going down is refused with both types named |
| Cast To a game type | one node per type | As before: the value as an object of the type, through Valid or Not Valid |
| Cast To a game class | one node per game class without a type of its own (`Classes/TiP/Cast To Pinata`) | The value as one of the class: an object of the class's type that the class's test takes |
| Missing types | the compiler | A variable or param of a type that went away (a class removed, a mod no longer there) is an error |

A graph file keeps an Object's type beside its kind (`"type": "object", "object": "TiP/Pinata"`); older RexFabric reads
it as any object.

Stage 3 adds enums, structs and containers to graphs:

| Part | Where | What it does |
|---|---|---|
| Enum and Struct | variables, params, pins | Of an enum or struct the mod may use. A game enum's values go into an Int and an Int into it, as they're the game's numbers |
| Holds | variables, params, pins | One, an array or a map, with a map's key (Bool, Int, String or Enum). A plain Any pin takes a whole array or map; otherwise containers and keys have to match |
| Starting values | the variable's details | Arrays, maps, structs and enums are filled in the way the content editors fill them |
| Make and Break | Structs, one of each per struct | A value from its fields, and its fields from a value |
| Enum value and text | Enums, one of each per enum | A value picked on the node, and its display name |
| Map nodes | Tables | Find (with Found), Add, Remove, Keys, Values, Count, For Each Entry and Clear, each typed by the map linked in |
| Array nodes | Tables | Contains, Add and For Each type their items by the array linked in |

In Lua an array is a table from [1], a map a table under its keys, a struct a table of its fields, a mod enum's value
its name and a game enum's its number. A graph file keeps the container beside the type (`"container": "map", "key":
"enum", "key_object": "MyMod/Colour"`) and a starting value as content keeps one (content/values.h).

Not yet:

* Casts to mods' classes, and values of them: nothing can be one until mods spawn their classes (stage 6), so a
  variable can already be typed with one, and a cast to one is always Not Valid.
* Class of a type (what a spawn node takes), also with stage 6.
* The checks at runtime in the compiled Lua; types are checked as the graph is edited and compiled.
* Switch on a mod's enum: Switch takes Int, String and the game's enums; compare a mod enum's value with Equal.
* Typed arguments for the game's functions beyond the object types `Namespace::Args` already gives them.

## Data tables

A data table is a table of rows of one struct type, each row under a name. A mod makes one in the browser by choosing
the row struct; it opens as a grid, a column per field and a row per entry, where rows are added, removed, reordered,
renamed and edited in place. Row names follow the asset name rules, so a row is referred to as `table, row` without
ambiguity.

Graphs and Lua read tables through typed nodes and functions:

| Node | Does |
|---|---|
| Get Row | The row of a name, as the table's struct, with a found and a not found path |
| Get Row Names | Every row's name, as an array |
| For Each Row | Each row in order, with its name |
| Has Row | Whether a name is in the table |

### The game's tables

The game's API can declare data tables of its own, filled from the game's data rather than a file, and read only: a mod
reads them as it reads its own tables, but can't change their rows in the editor. Changes to what they describe go
through the API's functions, so the game's rules still apply. For Trouble in Paradise:

| Table | Rows |
|---|---|
| `TiP/Pinatas` | every pinata species: its tag, name, class, requirements as text, price, level |
| `TiP/Items` | every item the game deals in: its tag, name, class, price, unlock level |
| `TiP/ShopItems` | the shops' unlock table (`shopUnlockables`): the item, its level, its triggers |
| `TiP/Requirements` | each species' requirements, by kind |

A game table's rows are made when it is first read and made again when the game says its data changed (a mod set a
price, the randomizer moved a requirement), so a table always shows the game as it is now. A game table also helps in
the editor: a `tag` property can offer the rows of `TiP/Pinatas` as a picker instead of a number.

### String lists

A string property's picker can also offer a game's string list (`content::DeclareStringList`): a list the game makes
each time a picker opens, named by the property's `of` just as a table is. Unlike a table it has no rows of a struct,
so it can mix things no single table holds. Trouble in Paradise's `TiP/AnyEffect` lists the loaded FX Emitter assets by
path first, then the game's effects by name, and the `TiP/FxEmitter` component picks its effect from it.
`content::StringChoices(of)` gives what a string of `of` is picked from: the list's strings, or the table's row names.

### What is built

The registry is `src/content/tables.cpp`; mods' tables load after the enums and structs, as their rows use them.

| Part | What it does |
|---|---|
| Table assets | Made in the browser (New Data Table) by naming them and picking the struct of their rows |
| Table editor | A grid: the name, then a column per field, each edited as the property editor edits it; rows added, removed, reordered and renamed. A row name that isn't valid or is there twice, and a row that doesn't fit the struct, are listed as problems |
| Changing the struct | Each row keeps what still fits the new struct |
| The game's tables | Under Game Tables at the top of the browser's tree, each opening read only: its rows as text, with Read Again |
| Graph nodes | Get Row (Found and Not Found), Has Row, Row Names and For Each Row, in Data Tables, one of each per table; a row name is picked on the node from the rows the table has |
| Lua | `rex.content.tables`, `table`, `row`, `row_names` and `has_row` |

A game declares a table with a function filling its rows (`TableDecl::rows`) through a builder: `Row` starts a row,
`Set` sets a field (a struct field's own fields as `field.inner`), `Add` adds to an array and `Put` puts under a map's
key. RexFabric calls it when the table is read and its rows are out of date, on whatever thread reads it; it says no
when it can't now (off the thread the game's data lives on, or before that data is there), and the game makes the rows
later with `FillTable`. `TableChanged` marks them out of date. Rows a game gives are checked as a mod's are. The game
reads one row back with `FindTableRow(table, row)`, its fields as JSON text (none while its table hasn't the row or
hasn't been made): Trouble in Paradise finds a variant requirement's species' tag in its `TiP/Pinatas` row.

A rename of a struct, an enum value or a struct field reaches tables' rows as it reaches classes. Not yet: row names
aren't picked from a table in property editors (a `tag` property is still a number), and renaming a row doesn't
reach the graphs and Lua naming it.

## Localisation

Every `text` property and every enum's display names are translatable. A mod's texts are kept in string tables, one per
language, beside its content: the editor writes the mod's own language's table as texts are typed, and a translation
editor shows every text of the mod with its translation in another language side by side, marking the missing and the
out of date ones. At runtime a text shows in the game's current language, or in the mod's own language when it has no
translation.

The game's API can go further and register a localisation editor for the game itself. It names the game's languages,
lists the game's own texts (in Trouble in Paradise, every loctext asset and key, as `tip.text` reads them) and says
how a translation is applied (for Trouble in Paradise, through the text hook). With it a mod can add a language to the
game: a language asset holding a translation of the game's texts, which the editor fills key by key from a language the
game has, and which the game offers in its language setting once the mod is enabled. A language asset can also cover
only some texts, to correct or restyle a language the game already has.

### What is built

Mods' own texts (`src/content/localisation.cpp`). The game declares its languages (`DeclareLanguages`: each language's
code and name, the first being the one a mod writes in when it doesn't say, and how to tell which one it shows now). A
mod says which it writes in with `language` in its `mod.toml`, picked in the metadata editor.

| Part | What it does |
|---|---|
| Texts | Every `text` value in a mod's classes, structs and tables, nested ones too, and its enums' display names |
| Keys | A text's key is where it is: the asset, a colon, then the property, field, row or enum value, then each struct field after a dot, each array element by its number from 1 and each map entry by its key in brackets (`MyMod/Spots:Home.label`). A struct field a value lacks, or has as the struct has it, is the struct's own text (`MyMod/Spot:label`) |
| String tables | `mods/<Mod>/localisation/<language>.json`, one per language: each key's translation and the source it was made from |
| Translation editor | The Translations tile at a mod's root: every text beside its translation into a language picked from the game's, each marked missing, out of date (its source changed since) or done, with Still Right for one that fits the new source; filtered by those, and searched. Translations of texts the mod no longer has are counted and can be dropped |
| At runtime | A text reaches Lua (properties, defaults, struct values, table rows, enum display names) in the language the game shows, from the mod's table for it, as written when there's no translation or the language is the mod's own. An out of date translation still shows until it's fixed |
| Renames | A renamed or moved asset or folder takes its keys along in every string table of its mod |

### The game's texts

Language assets (`src/content/game_texts.cpp`). A game that lets mods change its texts declares how
(`DeclareGameTexts`): `list` gives every text as the language the game shows has it, in groups, each text with an id
that's the same in every language and a label where the game knows one; `apply` takes the mods' texts by language,
then id, whenever they change. Listing follows the game tables' rule: a `list` that can't run on the reader's thread
says so, and the game calls `FillGameTexts` from its own; `GameTextsChanged` has them listed again.

| Part | What it does |
|---|---|
| Language asset | A content asset of the kind `language`: the language it's for, picked from the game's, and the mod's text for each game text it changes, by id, with the game's text it was written over |
| New Language | In the browser's Add menu and a folder's right click menu, when the game declared its texts: a name and the language, the one the game shows to start |
| Language editor | The game's groups down the side, every group or one; each text's label or id, the game's text, the mod's text in its place (as tall as its lines) and its state: Changed with Reset, or Out of date (the game's text differs from the one it was written over) with Still Right. Filtered to the changed or the out of date, searched by label, id and either text, a page of 100 at a time; Read Again lists the texts again with the labels learned since. Texts for ids the game doesn't have are counted and can be dropped |
| Another language | The game only has the texts of the language it shows, so an asset for another language is edited over those: the editor says so, a text typed then keeps the source it had, and whether one is out of date is told once the game is in the asset's language |
| At runtime | Every loaded mod's language assets load with its content, in load order, a later mod's text for an id over an earlier one's; out of date texts still show. The game shows them while it's in their language |

Trouble in Paradise (DeTiP's `src/modding/game_texts.cpp`) looks a text up by its loctext asset and a key, and the
asset keeps only a 16 bit hash of each key (`locTextGetText`, 82241050: `h = 33 * h + c` over the key's bytes) as the
string's id, with a table naming the keys whose hashes collide and the ids they get instead (8 byte entries at the
asset's +4: the name, the hash, the id). So its key names can't be listed from its data, but its strings can: a group
is a loctext asset (every one `assetDbGetList` finds for `aid_loctext_*`, named without that start), a text's id is
the asset's name, `#` and the string's id in 4 hex digits (`aid_loctext_pinata_text_xuiscene_frontendpressstart#0a39`),
and its label is its key where the collision table names it or the game looked it up since it started. The text hook
finds a lookup's id the same way and shows the mod's text for it.

## Textures

A mod imports a picture in the content browser (Import Image in the Add menu or a folder's right click menu, then the
system's Open dialog), and it becomes a texture: an asset of the built in class `Texture` (below `Object`), its file
holding what it's for, the picture beside it under its name.

```
mods/MyMod/content/Pictures/Logo.json   {"asset": "texture", "help": "The shop's sign"}
mods/MyMod/content/Pictures/Logo.png    the picture (PNG, JPEG, BMP, TGA or GIF)
```

The picture goes wherever the asset goes, as any file beside an asset named after it does: a rename, a move, a
duplicate or a delete takes it along.

| Part | What it does |
|---|---|
| Import Image | The Open dialog (Windows' own, or zenity or kdialog elsewhere; SDL's are built out), on a thread of its own, then a name, its file's to start, as far as the name rules let it be |
| The tile | The picture itself, as large as the tile allows |
| Texture editor | The picture as large as the window allows, its file and size, what it's for, and Replace, which takes another picture in its place at once (dropping the old one in another format) |
| Properties | A property of type asset of `Texture` picks from the textures the mod may use: its own and its dependencies' |
| For the game | `LoadTextureImage(path)` (`rexfabric/content.h`): a loaded mod's texture's picture as RGBA8, with a version that changes when its file does. `TextureImageVersion(path)` gives that version without reading the picture, for a game keeping what it made of one |

What a game does with one is its own: Trouble in Paradise makes it one of its textures (DeTiP's docs/modding.md,
Mods' pictures), which a signpost shows.

## Meshes

A mod imports a 3D model in the content browser (Import Mesh in the Add menu or a folder's right click menu), and it
becomes a mesh: an asset of the built in class `Mesh` (below `Object`), its file holding what it's for, the model beside
it under its name, as a texture's picture is.

```
mods/MyMod/content/Town.json   {"asset": "mesh", "help": "The town round the garden"}
mods/MyMod/content/Town.glb    the model, binary glTF with its textures inside it
```

| Part | What it does |
|---|---|
| Import Mesh | The Open dialog for models (`file_picker::Files::kMeshes`), then a name, its file's to start |
| The tile | A shaded box |
| Mesh editor | What it's for, the model's file and size, what it holds (nodes, meshes and their parts, materials, textures, read from the glTF's description alone), and Replace, which takes another model in its place at once (`world_mesh_editors.cpp`) |
| Properties | A property of type asset of `Mesh` picks from the meshes the mod may use |
| For the game | `MeshModelOf(path)` (`rexfabric/content.h`): a loaded mod's mesh's model file, for the game to read and draw |

What a game does with one is its own: Trouble in Paradise draws it with its native renderer in place of the garden's
backdrop (DeTiP's docs/modding.md, Meshes).

## Worlds

A mod's world is one of the game's worlds as the mod changes it: an asset of the built in class `World` (below
`Object`), made with New World in the content browser, its file naming the world it's built on, its base.

```
mods/MyMod/content/TownGarden.json   {"asset": "world", "base": "TiP/Garden", "help": "The garden in the town"}
```

| Part | What it is |
|---|---|
| Its base | A world of the game's, or another mod's world (eight deep at most): the world it's entered as |
| Its map edits | Map edits of the world change it alone; when it's entered, its base's map edits are made first, then its own, so an edit of its own can change what the base's made |
| Entering it | The game says it's in the world (`SetWorld`) where it would say the base, when something asks for it (Trouble in Paradise's signposts and `tip.enter_next`). `GameWorldOf(path)` gives the game's world a world comes down to, so the game knows which it may be entered in place of |
| World editor | Its base, picked from every world but itself, and what it's for (`world_mesh_editors.cpp`) |
| Properties | A property of type asset of `World` picks from the mods' worlds the mod may use |

`Worlds()` lists the game's worlds and then the loaded mods', so New Map Edit and the map edit editor offer them all.
Worlds are read with the map edits (`LoadMapEdits`), before them; one whose bases don't come down to a world of the
game's is logged.

## Game asset types

RexFabric's own kinds of asset (classes, enums, structs, tables, languages, textures, meshes, materials, worlds and map
edits) are what any game needs. Anything that only one game has is the game's to declare: a game asset type is a kind
of asset the game adds beside them, with `DeclareAssetType` (`rexfabric/content.h`), before `Start`. RexFabric knows
nothing of what one means. It reads, checks, edits, renames, translates and lists its assets, and the game reads them
back and does what they say. Trouble in Paradise's packets (kinds of ground a player buys and pours), star menu
leaves and pinata colour variants (patterns, palettes and variant requirements) are game asset types.

A type is a class, under a path of the game's own (`TiP/Packet`), whose assets are files saying the type's kind under
"asset". Its fields are at the top level of the file beside it:

```
mods/MyMod/content/RedSand.json
{"asset": "packet", "base": "Sand", "name": "Red Sand", "help": "Sand from a red shore", "icon": "MyMod/RedSandIcon",
 "look": {"colour": "MyMod/RedSandColour"}, "unlock": "Shop", "level": 3, "price": 500, "order": 1}
```

### Declaring one

| Part of `AssetTypeDecl` | What it is |
|---|---|
| `path` | The class its assets are: an asset property of it picks one of them |
| `kind` | What its files say under "asset" (`packet`): unique, and none of RexFabric's own kinds |
| `label`, `help`, `colour` | How the content browser names it (New Packet, its tiles, its editor's title), says what it is (its New entry's tooltip), and colours its tiles |
| `category` | The submenu of the browser's New menus its New entry is in (`UI`, `Variants`): one of the game's own, or RexFabric's Basic or Advanced, which it joins below their own entries. Empty for Other, the last |
| `fields` | Its fields, each an `AssetFieldDecl`: a name, a type (any property type, in any container), a default, a label, help, a condition (below), and `custom`, for a field its own editor alone edits (Own editors) |
| `create_with` | The fields a new asset is asked for before it's made (a packet's base) |
| `name_field` | A text field a new asset's name goes into, as a start |
| `check` | What's wrong with an asset, as the game sees it: each problem a line its editor shows, and the reason it isn't loaded |
| `warn` | What's doubtful about an asset that loads all the same: each a line its editor shows below the problems, in the warning colour. A check runs as the assets load, before the others are there, so it looks at the asset alone; `warn` runs only while the editor is drawn, and may look at what the asset names (a palette's pattern) |
| `built_in` | The game's own assets of the type, read only, listed first and offered in every picker of it |
| `preview` | A 2D preview of an asset as its fields are now, drawn at the top of its editor (Previews) |
| `editor` | The type's own editor, drawn with ImGui below the field table (Own editors) |
| `viewport` | A live 3D viewport of an asset as its fields are now, drawn by the game's viewport renderer and played with Play, Pause, Restart and Loop (Asset viewports) |
| `events` | Game events fired for one asset of the type, each with its node's title: with any, each asset has a graph of its own (Asset graphs) |
| `asset_events` | The event nodes an asset's graph has for its own fields, given its fields: a shop catalog's On Bought of each of its actions. A type with these has a graph even without `events` (Asset graphs, Field events) |

A field can show only while a field declared before it holds one of some values (`when_field`, `when_values`: an enum
value's name, a bool's `true` or `false`, a number as text). One type then holds several shapes: a packet's shop
level and price show only while its unlock is `Shop`. Two fields may share a name under conditions that never hold
together, each typing the value its own way: a packet's `look` is a struct of grass's look for a grass packet and of
the ground's look for sand and snow. A field hidden by its condition isn't loaded, checked or saved.

### What RexFabric does with them

| Part | What it does |
|---|---|
| New | Each type adds New `label` to its category's submenu of the content browser's New menus, asking for a name and the type's `create_with` fields. The new asset has every field shown for it at its default, and its name in `name_field` |
| The tile | Its thumbnail (The content browser, Thumbnails): its Thumbnail field's picture, else its preview, else a picture made of its viewport; a card in the type's colour with the first letter of its label for a type with none of these, and while the picture is made |
| The thumbnail field | Every type gets a `thumbnail` field, a Texture asset, last in its field table as Thumbnail, unless it declares one of that name itself. It's left out of the file while it's empty, and it's in the JSON the type's functions are given, as any field is |
| The editor | One editor for every type (`src/ui/game_asset_editor.cpp`): each field shown, as its condition says, edited as any value is (but the `custom` ones of a type with its own editor), then the type's own editor, then the problems: a value that doesn't fit its field, then the type's check; then its warnings. A type with a viewport has it beside all that (Asset viewports). Editing the field a condition reads shows the fields that go with it; a field shown as another of the type's fields than before (a packet's look, when its base changes) starts over at that field's default, and one shown again keeps its value where it still fits. Undo and redo bring back the whole asset |
| Files | Written with "asset" first, then the fields shown in the order they're declared; keys of no field of the type are kept as they were |
| Properties | An asset property of the type's class picks from the game's own assets of it and the assets of it the mod may use |
| References | Asset values in its fields are references like any other: renaming or moving a texture repoints them, and deleting one lists them |
| Texts | Its text fields are the mod's texts (`RedSand:name`, `RedSand:help`), translated in its string tables |
| For the game | `GameAssets(type)` lists every asset of a type: the game's own in their order, then the loaded mods', by mod load order, then path. `FindGameAsset(path)` gives one. Each has its path, its mod, and its fields as JSON text (each fitted to its type, defaults filled in, texts in the language the game shows) and its single fields as values, so a game reads them with whatever it parses JSON with. `GameAssetsRevision(type)` goes up each time the type's assets load again, for a game keeping what it made of them |

### Previews

A type's `preview` is given an asset's fields (as `GameAsset::json` has them, untranslated) and gives the layers of a
picture, bottom first, which the editor draws in a square at the top of its first column every frame:

| Part of `PreviewLayer` | What it is |
|---|---|
| `texture` | A Texture asset, drawn as its picture |
| `file` | An image file, when there's no texture |
| `shape` | A filled outline when there's no picture: points as fractions of `rect` from its top left, in order round it, concave or not |
| `rect` | Where in the square: left, top, width and height as fractions of its side |
| `colour` | The shape's fill, or the picture's tint |
| `stretch` | A picture fills its `rect` exactly, as a game stretches a picture over a box, rather than fitting it with its shape kept |
| `clip` | The part of the square the layer is cut to, as `rect` gives one; none while its width is 0 |

Trouble in Paradise's star leaf draws its leaf in its colour (or its own leaf picture) with its icon over it.

### Own editors

What a field table can't show well, a type draws itself: its `editor` is drawn with ImGui into the asset's editor,
mouse and all, below the field table, every frame the editor is drawn. Trouble in Paradise's pinata palette draws a
swatch for each grey of its pattern beside the pattern's picture coloured through the palette.

The editor draws it below the table rather than in its place, so the fields that the table edits well (the palette's
pattern, an asset picker) stay there and a type draws only what it adds. A field marked `custom` is left out of the
table, as the editor edits it; it loads, checks and saves as any field does.

| Part | What it is |
|---|---|
| What it's given | The asset's path, and its fields as JSON text as the editor holds them now: every field shown for it there, as the file has them (not fitted, so read them as a check reads a file) |
| What it gives | True when it changed them, the text then holding the asset's fields as they are now. The editor takes that as it takes an edit in the table: shown again over its fields' conditions, undone and redone (an edit is kept in the history once no item is active, so a drag of a colour picker is one edit) and saved with the rest |
| Pictures | `EditorImage(key, version, width, height, rgba)` (`rexfabric/content.h`) gives the UI's texture of pixels the game made (RGBA8), for `ImGui::Image` or a draw list's `AddImage`. It's kept under the game's key (the asset's path in it, so two open editors don't share one) and made again only when `version` changes, so a game gives the same version until its pixels do. It works inside an editor alone; one not asked for in a while is freed (`MenuModel::CodePicture`) |

### Asset viewports

A type whose assets are seen best moving (an effect playing) declares a `viewport`, an `AssetViewportDecl`: its
editor then shows a live 3D viewport of the asset as the editor has it, saved or not, so an edit shows at once. It's
the viewport a class has (The viewport), drawn by the same viewport renderer the game registers, with the same camera
(drag to go round, the wheel to come nearer, Reset View), but with playback controls in place of the component tools:

| Control | What it does |
|---|---|
| Play, Pause | Whether its time runs (`ViewportView::playing`); paused, what it shows holds still |
| Restart | Everything from the start (`ViewportView::replays` goes up) |
| Loop | Whether what it shows starts again once it's done (`ViewportView::looping`), or plays once and stays done. It starts as the declaration's `looping` says |
| The time | How long it's played since it last started, in seconds: the game's `ViewportImage::time`, or counted by the editor while the game doesn't say |
| Capture Thumbnail, Clear | The camera and the time as the asset's thumbnail framing, or the default framing again (The content browser, Capture Thumbnail) |

What playing, looping and starting over mean is the game's to say: RexFabric only passes them on.

| Part of `AssetViewportDecl` | What it is |
|---|---|
| `backing` | The backing the renderer shows, as a class's viewport shows its class's (`ViewportView::backing`); empty for none, the scene's parts alone |
| `build` | Builds what the viewport shows of an asset, an `AssetViewportScene`, from its path and its fields as the editor has them now (as `GameAsset::json` has them, untranslated): the values its backing is shown with and parts on the stage beside it, each a `ViewportPart` the renderer shows as it shows a component of its type in a class's viewport. A part's type needn't be a declared component, only one the renderer knows. It's called as the editor opens and again whenever the fields change, not every frame; a scene unlike the one it gave last starts everything over, as Restart does. It may throw, and the editor shows the reason over the viewport |
| `looping` | Whether Loop starts on |

A declaration with neither a backing nor a build is refused. The renderer knows an asset's viewport from a class's by
`ViewportView::asset`, the asset's path, set only for an asset's (`class_path` then empty).

An asset without a graph is laid out as a material is: the viewport on the left, as wide as its edge is dragged, and the
fields, the 2D preview, the type's own editor and the problems beside it. An asset with a graph has the viewport as its
Viewport tab, in place of the 2D preview. The viewport is made when it's first drawn and closed with its editor.

Trouble in Paradise's FX Emitter plays the effect its editor holds so, recompiled as its fields change (DeTiP's
docs/modding.md, FX).

### Asset graphs

A type naming `events` or `asset_events` gives each of its assets a node graph of its own, Blueprint style: edited in
the asset's editor and kept beside its file, as the asset's companions (they move, copy and go with it):

```
mods/MyMod/content/Leaves/Maze.json         the asset
mods/MyMod/content/Leaves/Maze.graph.json   its graph
mods/MyMod/content/Leaves/Maze.graph.lua    the Lua it compiles to, run after the mod's scripts
```

| Part | How |
|---|---|
| The events | Each is a game event (`Namespace::Event`) whose Args name the asset's path first: `tip.star_leaf_pressed(leaf, pinata)`. A new graph gets a node of each down the left of its event graph (`graph::PlaceEventNodes`), titled as the type says ("On Pressed"), which can't be deleted, copied or duplicated. Each is `asset.event:<event>`, a twin of the event's own node kept out of the add menu, without the asset's pin |
| What it compiles to | The event's own `rex.on`, which returns at once unless the first value is the graph's asset: `if Leaf ~= Asset then return end`, `Asset` being `rex.content.script_asset()`, worked out when the script loads from where it is. So a renamed or copied asset's graph is its new asset's without compiling it again |
| Running | `LuaScripts` (mods.h) gives each mod's scripts folder, then every `*.graph.lua` in its content folder; a script there counts as its mod's (lua_vm's `CallerMod`) |
| The editor | Laid out as a blueprint, and drawn as the node editor is (its glass theme and the canvas's light behind the panes, `AssetEditor::HoldsCanvas`): the toolbar, then the tabs across the window (Viewport, the type's viewport or else its preview as large as fits; Event Graph, the canvas; Lua), which stay put whichever is open, and under them the panes: the graph's functions, macros, variables and dispatchers on the left, only on the Event Graph and Lua tabs; the open tab's page in the middle; on the right the details, the asset's fields and problems on the viewport and the selected node's on the graph. Each side pane is as wide as its edge is dragged. The canvas is the node editor's own (`GraphView`, `src/ui/graph_view.h`, which the node editor window shows too). The layout is `OwnGraph` (`src/ui/own_graph.h`), which an avatar class's editor shares (Class graphs). The fields and the graph are one document to undo and redo: a settled edit of the canvas goes into the editor's history with the fields, and an undo that changes the graph puts it back on the canvas |
| Saving | Writes the asset, its graph and its script, then runs the mods' scripts again (as the node editor's Save and Reload), so the graph runs at once |

The node editor and the asset graphs come with `WITH_NODE_EDITOR`; without it a type's assets are edited without
their graphs.

#### Field events

Some events only make sense for a part of an asset: a shop catalog fires `tip.catalog_action_bought(catalog, action)`,
and each of its actions wants a node of its own. A type's `asset_events` gives those nodes from the asset's fields
(as `check` gets them, fitted and untranslated): an `AssetEventDecl` for each, with its event, its title
("OnBought_Romance") and `match`, the values the event's arguments after the asset must equal for the node to run
(`{"Romance"}`). An empty match runs for every firing for the asset, as a node of `events` does.

| Part | How |
|---|---|
| The nodes | Each is the event's `asset.event:<event>` node with two settings of its own: `Match`, the match as a JSON array of strings, and `Title`, its title (the event's own when empty). Like the `events` nodes they're down the left of the event graph, a new one below the event nodes there, and can't be deleted, copied or duplicated. Their outputs are the event's arguments after the asset, as the other asset events have |
| Who it is | A node is known by its event and its match, never by its place in the fields, so reordering a catalog's actions keeps every node and its links |
| Following the fields | The editor asks `asset_events` again whenever an edit of the fields settles (not every frame, and not while a value is still being typed) and makes the graph's nodes what it gives (`graph::PlaceFieldEvents`, `src/graph/node_library.h`): a node for a new one, and a node whose event and match it no longer gives goes. What was linked from a node that went stays where it was, unlinked: a ghost, as Blueprint leaves the nodes of a removed event, so the work isn't lost and can be linked to another event. The change is part of the edit that caused it, so one undo brings back the field and its node with its links |
| Renaming | A renamed action is a new match, so its old node goes (its chain left a ghost) and a new one is added. A node whose title alone changed, its match the same, keeps its place and links and takes the new title |
| What it compiles to | The event's own `rex.on` with the asset guard, then a guard on the values after the asset, in order, each compared as text, so a number the game fires matches the text of it |
| Saving and loading | They're saved in the graph's file as any node is. Opening the editor asks `asset_events` of the fields as the file has them and makes the nodes so, as if they'd always been, for fields changed outside the editor. A type whose `asset_events` throws keeps the nodes the graph has, and the editor says why |

A shop catalog's node for its action Romance compiles to:

```lua
local n7_Asset = rex.content.script_asset()

-- OnBought_Romance
rex.on("tip.catalog_action_bought", function(n7_Catalog, n7_Action)
  if n7_Catalog ~= n7_Asset then return end
  if tostring(n7_Action) ~= "Romance" then return end
  rex.info("bought", n7_Action)
end)
```

Game assets load with the classes (`LoadGameAssets`, `src/content/game_assets.cpp`), after the enums and structs. One
whose value doesn't fit a field, or that the type's check finds wrong, is logged and left out. Trouble in Paradise's
packet type, what it does with packets and how it sorts them by their `order` are DeTiP's (docs/modding.md, Packets).

## Map edits

A map edit is an asset that changes one of the game's worlds each time the game enters it: the class `MapEdit`
(built in, below `Object`), made with New Map Edit in the content browser, its file holding the world and an ordered
list of edits. It's where placed avatars live: a mod places its signposts on the main menu with a map edit, not a
script.

```
mods/MenuSigns/content/TitleScreen.json
{"asset": "map_edit", "world": "TiP/TitleScreen", "edits": [
  {"name": "Hello", "kind": "Spawn", "class": "MenuSigns/HelloSign",
   "transform": {"position": [120.5, 3.2, -48.0], "rotation": [0, 30, 0]}, "values": {"text": "Hi there"}}]}
```

| Part | What it is |
|---|---|
| Worlds | The game's places, declared by the game (`DeclareWorld`), which says each tick which it's in (`SetWorld`, empty for none). Trouble in Paradise's: `TiP/TitleScreen` and `TiP/Garden`, the player's. Mods' worlds (Worlds, above) are built on them: a map edit of one is made only when the game is in it, and the base's are made there too |
| An edit | A name (unique in its map edit), a kind, a transform in the world, and values |
| Spawn | RexFabric's own kind: an avatar of a class (one the mod may use, with a backing) where the transform says, with the values it gives set on that avatar alone, over its class's |
| The game's kinds | Declared by the game (`DeclareMapEditKind`): properties as a struct's fields are, whether it has a transform, how it's applied to the world (a handle back) and undone, and, when it can, how what it made is moved to a new place. For what changes what's there already: a model swapped for another, an actor taken away. Trouble in Paradise declares `TiP/ReplaceModel`, `TiP/HideObject` and `TiP/ShowModel` |
| When they're made | On RexFabric's tick, when the game enters a world (a new path, or the same after an empty one), and again when the mods' content is loaded again (a save in an editor): what was made before is undone, then every enabled mod's map edits for the world, in load order, each one's edits in order |
| Previews | An open map edit editor shows its map edit in the world as it has it, saved or not (`PreviewMapEdit`, until the editor closes): on the next tick what changed is changed to fit, each edit found by its map edit and name. A spawn that only moved is moved (`MoveAvatar`), one whose values changed has them set (`SetAvatarValue`), a game kind's edit that only moved is moved by its kind's `move` when it has one, and anything else (another class, a value no longer set) is undone and made again. Edits gone from the map edit are undone, new ones made |
| Undone | Only while its world is still there (the content loaded again): a spawn's avatar is removed, a game kind's edit undone. A world left takes its own with it: the game destroys the spawned avatars with its scene, so they're left to go as any avatar whose world went does, and removing them as well would free them twice |
| Kept | Avatars made so are avatars like any other: Lua sees them, a cast to their class works, their components tick. When one goes (a reload of the scripts removes every avatar but the saved ones), the world's edits are made again, at most once a second. One that went with its world (its backing gone, `WentWithItsWorld`) means the game left the world before saying so, as when a load pops the scene within one of its ticks: nothing is made again until the game names the world it's in next. Map edits are never saved with a world (Saved with the world) |

The map edit editor (`MapEditEditor`) shows its world, whether the game is in it now (what it changes shows there as
it's changed), and each edit as a tree node with up, down and remove, and under it in red what went wrong making it in
the world the last time (`MapEditProblem`: a spawn its class refused, a game kind that found nothing to change): its name, its kind, a spawn's class (picked from the avatar classes
the mod may use), its transform, and the properties of its class (or kind), a value set here in blue with a reset to
the class's. Its problems: a world the game doesn't have, a name that isn't valid or is taken, a class that isn't one,
isn't an avatar class or belongs to a mod it doesn't depend on (with Add as a dependency), a kind the game doesn't have,
a value for a property there isn't or of the wrong type. Renaming or moving a class repoints the map edits spawning
it, as it repoints the classes naming it.

### Editing in the world

With the game in a map edit's world, its editor edits it in place: Edit in World puts a marker on the game's picture
where each edit is, its name beside it, and ImGuizmo's gizmo on the selected one (`src/ui/map_edit_world.cpp`). As the
world shows the editor's map edit (Previews, above), a drag moves the avatar along with the gizmo.

It needs the game's view of its world, registered with `SetWorldView` (`rexfabric/content.h`):

| Part | What it is |
|---|---|
| `camera` | The camera the game's picture is drawn with now (`WorldCamera`: view and projection as row vectors, the view rigid, the projection onto the whole picture, and the eye); false while no world is shown. Any thread |
| `aim` | The rays the editor wants tested (`WorldRay`: from one point toward another), replacing the ones before. Any thread |
| `hits` | What the rays last tested hit (`WorldHit`: the ray and the point it met first, none for nothing), tested by the game on its own time |

| In the world | What it does |
|---|---|
| A marker | Every edit with a place (a spawn, or a game kind with a transform): orange, the selected one yellow, the one under the pointer lighter. A click selects it, opening its node in the editor |
| The gizmo | On the selected edit: Move, Turn or Scale (W, E, R while the editor has the keyboard), along its own axes or the world's. A drag changes its transform as it goes, to four decimals, and the whole drag is one step of undo |
| Place | The next click in the world places a new edit where the pointer meets it, of what's picked beside the tools (at first the selected edit's): a spawn of an avatar class, or one of the game's kinds with a place, which changes what's nearest. It's named after its class or kind; Shift keeps placing. Right click or Escape stops |
| Drop | Puts the selected edit down on what's under it: a ray straight down through it, from above |
| Delete, Ctrl+D | Removes the selected edit, or copies it two units along the world's x, with the editor's keyboard or the pointer over the world |

The overlay is a window of its own over the whole picture, behind every other window and taking no input itself: the
game keeps its clicks and camera, except over a marker or the gizmo, and everywhere while placing, where the overlay
asks for the mouse from the frame before the click (`SetNextFrameWantCaptureMouse`). The pointer's ray goes from the eye
through the pointer, its direction from the further of clip space's two depths, so it holds whichever way the game's
depth runs.

Not yet: worlds beyond the title screen. A garden is one the game saves: an edit there needs a rule for the save
first.

## Materials

A material is an asset saying how something the game draws looks: the class `Material` (built in, below `Object`),
made with New Material in the content browser. What a material can set is the game's to say, as only the game knows
its renderer: the game declares material types (`DeclareMaterialType`), and a material is of one of them and gives its
properties values. Whatever the game draws with a material reads it with `FindMaterial`.

```
mods/ModelProbe/content/RedBarrel.json
{"asset": "material", "type": "TiP/ModelMaterial", "help": "The barrel lit red",
 "values": {"tint": true, "tint_colour": "#FF3030", "game_textures": [["monkey_wood_colourmap", "common_effect_rainbow"]]},
 "preview": "pinata_avatar_prop_bigrock_default"}
```

| Part | What it is |
|---|---|
| A material type | Declared by the game (`MaterialTypeDecl`): a path, help, properties as a struct's fields are (containers too: a map of textures by slot), and its preview: one of the game's components shown alone in the editor, its values, the property the material's path goes in, and the property the editor lets its user pick (what it's shown on) |
| A material | Its type, the values it gives the type's properties (the rest are the type's defaults), its help, and what its editor previews it on (`preview`, for the editor alone) |
| `FindMaterial` | A loaded material as it is now, as its editor has it while one is open: its type, each single property's value, each map property's entries by key, and a version that goes up with any change, so a game keeps what it made of it until the version moves. Any thread |
| Previews | An open material editor gives its material as it has it (`PreviewMaterial`, until it closes): whatever is drawn with the material shows each change at once, in the world and in viewports alike |
| A property naming one | `asset` of the class `Material`: Trouble in Paradise's `TiP/Model` component has one |
| A graph material type | A type with graph inputs and shading models: its materials are node graphs, compiled to HLSL for the game's shader, edited in the material graph editor (docs/materials.md) |

The material editor (`MaterialEditor`) has the class editor's two columns: on the left a viewport showing the material
on the type's preview (Trouble in Paradise's: one model, with the material), drawn by the game's renderer as the
Avatar editor's is; on the right its type, help, what it's shown on, and each of the type's properties with its value,
blue with a reset where the material sets one. Its problems: no type or one the game doesn't have, a value for a
property there isn't or of the wrong type, and an asset from a mod it doesn't depend on. Renaming or moving a texture
repoints the materials naming it.

## Editors

Each class may name the editor its assets open in, and a class without one uses its nearest ancestor's. New editors are
registered by RexFabric or the game and attached to a class, so a later `Texture` class can open in a texture viewer
without anything else changing.

| Editor | For | Shows |
|---|---|---|
| Properties | `Object`, so every class by default | The class defaults as a list, grouped by the class that declared them |
| Avatar | An avatar class | A viewport, the component tree, the class defaults and the class's graph |
| Texture viewer (later) | `Texture` | The image, its size and format, its mips |

What is built is the properties editor, for every class (`src/ui/asset_editors.cpp`), which for an avatar class is the
Avatar editor: its viewport (The viewport) beside the rest, a wider window. The class's graph is still to come. Each editor is a window of its own
beside the browser, which owns it; a new editor is another `AssetEditor`, picked by the browser for what it opens.

* Class: its path, its parent (picked from the classes the mod may use), and its help.
* From each ancestor, under its name: every property it declares, with the class's value. A value set here shows in
  blue, with Reset going back to the parent's.
* Added here: the class's own properties, each with its name, type, default and help, and for an asset property the
  class the asset must be.
* Problems at the top: a parent that isn't a class or goes round in a circle, a property name that isn't valid or is
  inherited already, a default for a property the parent doesn't have or of the wrong type, an asset property's class
  that isn't one. A path in a mod that isn't a dependency is a problem with Add as a dependency as its fix, which writes
  it into `mod.toml`; a default the parent lacks has Remove it.

Ctrl+S saves, Ctrl+Z and Ctrl+Y undo and redo (a text field keeps its own undo). A class with problems still saves, as
graphs do, and doesn't load until they're fixed. The editor reads the classes the way the browser shows them: a mod's
class from its file, as saved a moment ago, so a class of a mod that's off still shows what it inherits.

## Avatars

What can be placed in the world is the game's to define, as the game alone knows what its world objects are. A game
registers one class as its `Avatar`, the base of everything it places, and may register subclasses for its own kinds
of avatar. Trouble in Paradise's world objects are avatars in the game itself: `avatarInst_s` begins with the object
system's `objInst_s` and adds a position at +0x68, so `Avatar` here is the game's own name for its placeable class.

An avatar class has, beyond its defaults:

* a component tree,
* a graph: an event graph made in the node editor, compiled to Lua as graphs are today, run for each spawned instance
  (Class graphs),
* a backing: what the game makes in the world for it. The game defines the backings it offers. For Trouble in
  Paradise these would be a decor or a pinata based on one of the game's own tags, or a plain world object with no
  game item behind it.

### Components

A component type is declared by the game: its path, its properties, whether it has a transform, and what it does when
attached, every tick and when detached. RexFabric only keeps the tree, the transforms and the property values, and
calls those lifecycle points.

Components have a transform relative to their parent component, or to the avatar for the root ones, so they move with
the avatar and can be placed and turned in the viewport. A component that does something constant needs no graph: a
`TiP/FxEmitter` set to loop plays its emitter for as long as the avatar exists, without timers in a graph.

Examples for Trouble in Paradise, each built on what the modding API already does:

| Component | Properties | Built on |
|---|---|---|
| `TiP/FxEmitter` (built) | the emitter, looping, how often | the fx spawning (`fx.cpp`) |
| `speech_bubble_trigger` | the text, who says it | the speech bubbles (`bubble.cpp`) |
| `happiness_aura` | the radius, the amount, how often | `pinata:add_happiness` |
| `TiP/Model` (built) | a model of the game's, its shadow and a material | the game's own avatars, given the model (DeTiP's docs/modding.md, Models) |

### What is built

`src/content/components.cpp` keeps the game's backings and component types, `src/content/avatars.cpp` the avatars in
the world.

| Part | What it does |
|---|---|
| Backings | Declared by the game for one of its classes (`DeclareBacking`): how to make one where it's asked for, as the avatar's values (`AvatarValues`) say, where it is now (false once its world is gone), how to move and remove it, the game's object it is, if any, and how to show values set on the avatar later (`apply`). A mod class whose nearest game class has backings is an avatar class: it names one with `backing`, or keeps its nearest mod ancestor's, or gets the first the game declared for its game class |
| Component types | Declared by the game (`DeclareComponent`): a path, properties as a struct's fields are declared, whether it has a transform, and attach, tick and detach functions given a `ComponentContext` (the avatar, the component's name, where it is in the world, its values, the seconds since the last tick and a state of its own) |
| A class's components | A tree in its file, each with a name unique along its ancestors, a type, a transform relative to the component above it (or the avatar) and the values it sets; a subclass has its ancestors' components and adds its own. Checked as the class loads: a name taken twice, an unknown type, a value that doesn't fit, or components on a class nothing gives a backing, and the class is left out |
| Spawning | `SpawnAvatar` makes the backing, then attaches every component in tree order, the ancestors' first. The avatar keeps its class as it was then; its property values start as the class's and can be set on it alone |
| Ticking | Each frame, before the frame event: an avatar whose backing is gone is detached and forgotten, and every component of the rest ticks where the avatar and the components above it put it. A component function that throws is logged, and a tick that throws isn't called again |
| Removing | Detaches the components in reverse order and removes the backing. A reload of the mods' scripts removes every avatar but the saved ones, disposing of them on the next tick |
| Saving | An avatar spawned saved (`SetAvatarSaved`) is kept in its world's save and spawned again when the world loads (Saved with the world) |
| Lua | `rex.content.spawn`, `remove`, `is_avatar` and `avatars`, and the avatar handle (Lua) |
| Graphs | The Avatars nodes: Spawn (one per avatar class the mod may use, its Avatar output typed by the class, with a Saved input), Remove Avatar, Move Avatar, Avatar Location, Is Avatar Valid, and Get Value and Set Value of a property of the avatar's class |
| Class editor | An Avatar section on an avatar class: the backing, picked from the game's for its game class or kept as its parent's; the inherited components listed; and its own as a tree, each with its name, type, position, rotation and scale, its type's properties with a reset to the default, up, down, remove and Add Child. With the node editor, laid out as a blueprint with the class's own graph (Class graphs) |
| Events | Each avatar fires RexFabric's avatar events, spawned, tick and removed, which its class graph runs (Class graphs) |

An avatar's handle is RexFabric's own, not a game object. Where Lua or a graph gives it to something taking a game
object, it goes as its backing's object, an error for a backing that's only a place; a cast to a game type or class
gives that object, nil for a place. A game object casts to a mod's class when it's the object of an avatar of it.

A game class can carry properties its backing reads: Trouble in Paradise's `TiP/Signpost` (DeTiP's docs/modding.md,
Signposts) has a text, a colour, an image and an action, which a mod's subclass sets and its backing makes a real
signpost of the game's from, the first avatar backed by one of the game's own objects.

Placed avatars live in map edits (Map edits). Not yet: the components' and backings' texts in other languages.

### Saved with the world

An avatar spawned saved belongs to the world it's in rather than to the script that spawned it: the game keeps it in
that world's save, and it's spawned again where it was when the world loads. A reload of the scripts leaves it; it goes
when it's removed or its world goes.

```lua
local statue = rex.content.spawn("MyMod/Statue", x, y, z, 0, {saved = true})
statue.saved = false   -- no longer kept
```

The game's save holds none of it itself. The game asks RexFabric for its part as it saves a world (`WorldSaveData`) and
keeps that beside its own data, then hands it back once the world has loaded (`RestoreWorldSave`):

```
{"format": 1, "avatars": [{"class": "MyMod/Statue", "at": {"position": [40, 0, -89]}, "values": {"colour": "Red"}}],
 "mods": {"MyMod": {"visits": 3}}, "game": {"packets": {"format": 1, "Sand": [...]}}}
```

Each record is a saved avatar's class, where it is, and the values it has that its class doesn't give it (so a class
whose defaults change gives the new ones to the avatars that kept them). A record whose class isn't loaded now (its mod
gone or turned off) isn't spawned: it's kept as it was and written into the world's next save, so a mod turned off for a
while finds its avatars again when it's back. A value whose property is gone is dropped with a warning. `mods` holds
each mod's own data, by its folder (lua_mods.md, World data): the Lua VM gives it as the world saves and takes it back
as the world loads (`ModDataSource`, `src/content/world_saves.h`), a mod not loaded keeping its own as it was. A world
with nothing to keep gives an empty string, and the game's save stays as the game alone would write it.

The game can keep parts of its own in the same data, for what it adds to its worlds for mods that isn't an avatar:
each part is declared once with `DeclareWorldSavePart` (`rexfabric/content.h`), a name with a `save` giving its JSON and
a `restore` taking it back. They go under `game`, by name, and each is restored after the saved avatars, with an empty
string when the save had none of it; a part a save holds that nothing declares is kept as it came and saved again.
Trouble in Paradise keeps the mod packets poured on a garden's ground this way (`"game": {"packets": {...}}`).

What a map edit makes is never saved: it's made again on every visit, and not at all once its mod is gone. Trouble in
Paradise keeps the mods' part in a section of its garden saves of its own, which the game without mods passes over
(DeTiP's docs/modding.md, Saves). `src/content/world_saves.cpp`.

### The viewport

The viewport shows one avatar of the class being edited, alone, drawn by the game with its own shaders, on a plain
background, and the class's components over it (`src/ui/avatar_viewport.cpp`). It draws the class as the editor has it,
saved or not: a value changed in the editor shows at once.

How it's drawn is each game's to say, as only the game knows its renderer. RexFabric draws nothing of the avatar
itself: a game registers a viewport renderer, which RexFabric asks for each viewport every frame it's seen, and which
gives back what it drew last and the camera it drew with (`rexfabric/content.h`):

```cpp
rexfabric::content::SetViewportRenderer({.show = ShowViewport, .close = CloseViewport});
```

| Part | What it is |
|---|---|
| `ViewportView` | What's asked: the class, its game class, its backing (its own or inherited), every property's value as the editor has it, every component as a `ViewportPart` (its name, type, place in the avatar's space and values, inherited or its own), the camera (yaw and pitch round the avatar, in degrees, and a zoom on the game's framing), the viewport's size, whether its time runs (Play and Pause), whether it loops (an asset viewport's Loop; always on for a class's), a count that goes up with each Replay, whether a gizmo is held, when the game should keep its framing still, and for a game asset type's viewport the asset's path (Asset viewports) |
| `ViewportImage` | What was drawn: the UI's texture of it (an `ImTextureID`) and the part of it filled, the view and projection it was drawn with (row vectors, as ImGuizmo takes them; the view only turning and moving, any scale in the projection), whether it's orthographic, a message to show over it (why there's nothing to see, or what a component couldn't show), and how long it's played since it last started, for an asset viewport's time readout. The image is drawn over the viewport's grid, so what the game leaves clear shows the grid |
| `show` | From the UI thread, every frame a viewport is seen, giving at once what was drawn last: the game draws on its own time, on its own thread |
| `close` | The viewport's editor closed: what the game made for it can go |
| `capture` | Optional: one picture of a view on a stage of its own, for the content browser's thumbnails, handed back a few frames later (The content browser, Making a picture) |

With no renderer the viewport says so ("No viewport renderer: the game has not registered one, so avatars can't be
previewed. Components can still be edited by their values.", logged once), and everything else keeps working over a
plain background. When the game has no camera to give, the viewport keeps a camera of its own, in perspective, round the
components.

Under the game's image the viewport draws the ground's grid (y = 0 in the avatar's space) with whichever camera there
is: lines a round number of units apart (1, 2 or 5 times a power of ten, near a twentieth of the camera's distance), the
two through the avatar's feet brighter, each cut where it passes behind the camera. It's drawn by the viewport itself,
not ImGuizmo, whose grid can't cut a line behind a camera of this projection.

| In the viewport | What it does |
|---|---|
| Drag (either button, away from the gizmo) | Goes round the avatar |
| Wheel | Nearer and further |
| A marker | Every component with a place is a marker where the avatar puts it, its name beside it: the inherited ones grey, the class's own orange, the selected one yellow. Clicking one of the class's own selects it |
| The gizmo | ImGuizmo's, on the selected component: Move, Turn or Scale (W, E, R over the viewport), along its own axes or the avatar's. A drag changes the component's transform as it goes, kept relative to the component above it, to four decimals; the whole drag is one step of undo |
| Reset View | The camera it started with |
| Capture Thumbnail, Clear | The camera as the class's thumbnail framing, or the default framing again (The content browser, Capture Thumbnail) |
| Pause, Play, Replay | Its time held or running (what the components do, how the avatar moves), and everything from the start, as when an avatar of the class is spawned |

A component is selected in the viewport or by its name in the tree, which shows it selected. ImGuizmo (MIT, commit
105e4fd) is in `thirdparty/imguizmo`, built against the SDK's ImGui as imgui-node-editor is, and included through
`src/ui/imguizmo.h`, which declares its functions as linked in rather than imported from ImGui's library.

Trouble in Paradise's renderer is the journal's avatar viewer (`XuiClassPinataAvatarViewer_c`) made without its XUI
element, drawing into a texture of its own that the native frame shows in the editor (DeTiP's docs/modding.md, The
avatar viewport).

Not yet: clicking the avatar itself to select (a game would have to say what's under the pointer), and the avatar placed
in the live scene as a second view. What a component does shows as the game previews it: Trouble in Paradise plays an
emitter's effect.

### Class graphs

Every avatar class has a node graph of its own, Blueprint style, which runs for each avatar of the class that's
spawned: its events are the avatar's, Self is the avatar, and its variables are each avatar's own. It comes with
`WITH_NODE_EDITOR`.

#### Files

The graph is kept beside the class's file, as a game asset's graph is (Asset graphs), so it moves, copies and goes with
the class:

```
mods/MyMod/content/Lights/Glow.json         the class
mods/MyMod/content/Lights/Glow.graph.json   its graph
mods/MyMod/content/Lights/Glow.graph.lua    the Lua it compiles to, run after the mod's scripts
```

`LuaScripts` (mods.h) runs every `*.graph.lua` of a mod's content folder after its scripts folder, so a class graph runs
as an asset graph does. The script finds its class from where it is (`rex.content.script_asset()`), so a renamed or
copied class's graph is the new class's without compiling it again.

#### The editor

An avatar class's editor is laid out as a blueprint, as a game asset with a graph is (`OwnGraph`, `src/ui/own_graph.h`,
which both share): the toolbar and the class's problems, then the tabs across the window (Viewport, the class's viewport
with its gizmos; Event Graph, the canvas; Lua, the script it saves as), and under them the panes: the graph's functions,
macros, variables and dispatchers on the left, beside the graph alone; the open tab's page in the middle; and on the
right the class's details (its parent, help, inherited values, own properties and the Avatar section) on the viewport,
the selected node's on the graph. The class and its graph are one document to undo and redo. Saving writes the class,
the graph and its script, then runs the mods' scripts again, so the graph runs at once. A class that becomes an avatar
class as it's edited (given a backing, or a parent with one) gets its graph then.

#### Events

A new graph gets a node of each of its avatars' events down the left of its event graph (`graph::ClassEventNodes`,
placed by `graph::PlaceEventNodes`), which can't be deleted, copied or duplicated. Each is `class.event:<event>`, kept
out of the add menu.

| Node | Event | When |
|---|---|---|
| On Spawned | `avatar_spawned` | Once the avatar is spawned and its components attached |
| On Tick | `avatar_tick`, with Seconds | Every frame, after its components ticked, with the seconds since the last |
| On Removed | `avatar_removed` | As it goes, removed or gone with its world, before its components detach. Self can still be read; for one gone with its world its backing is gone already |
| The game's | Each `ClassEventDecl` of the class's game class and its ancestors (`ClassDecl::events`), titled as it says | When the game fires it for the avatar, with the rest of its values as the node's outputs |

These are real events, fired by the avatar runtime (`src/content/avatars.cpp`) with the avatar's id first, so a plain
`rex.on("avatar_tick", function(avatar, seconds) end)` hears them too (content.h, `kAvatarSpawned`). A game fires its
class events the same way, with the avatar's id first as an int64, which it finds from a backing's handle with
`AvatarOfBacking(backing, handle)`. Lua gets that first value as the avatar itself, in a class graph and in a plain
`rex.on` alike (lua_rex.cpp's `DispatchEvent`, for every `content::IsAvatarEvent`); fired with an id no avatar has now, it
reaches no Lua at all. A game event of a class, used in a mod's ordinary graph, has its first output typed as an avatar
of the class that declares it.

A reload of the mods' scripts removes the avatars they spawned without their removed event, as the scripts whose graphs
would run it are gone. The saved avatars a reload keeps fire their spawned event again on the next tick, so the new
scripts' class graphs start each one's own (`content::AnnounceAvatars`).

Only these events and Custom Events go in a class graph: another event (On Frame, a hook, a game event that isn't one
avatar's) has no avatar to run for, so it's an error there and belongs in one of the mod's graphs. On Tick is where work
done every frame goes.

#### Self

Self is the avatar a chain runs for, typed by the class, so it goes into anything taking an avatar of the class or of a
class above it. The Self node gives it anywhere in the graph, functions and macros too, and as a blueprint's Target is
self, an Avatar input of the Avatars nodes left unlinked in a class graph is Self: Move Avatar, Remove Avatar, Avatar
Location and Is Avatar Valid act on it, and Get Value and Set Value read and set its property values. The add menu offers
a Get and a Set of each of Self's properties under Avatars/Self.

#### Running and inheritance

A class graph runs for every avatar of its class and of the classes below it. A subclass's own graph runs too, Blueprint
style: an event linked in a class's graph overrides the same event of the graphs above it, for the avatars of that class
and below, and its Call Parent node runs the parent's chain of the event (the nearest class above with one) with the same
values. An event left with nothing linked is a ghost, as a blueprint's placed events are: it overrides nothing, so a
subclass that only adds a tick of its own still runs its parent's spawned and removed chains.

Overriding rather than running both, parent first, is what Blueprint does, and it's the only way a subclass can replace
what its parent does rather than only add to it; Call Parent gives back the other at the one place it's wanted, before,
after or between the subclass's own nodes. Running both in order would also need the graphs to load in class order,
which scripts of several mods don't.

Each linked event compiles to `rex.content.on_class(Class, event, handler)`, `Class` being `rex.content.script_asset()`.
RexFabric keeps the handlers by event and class (lua_content.cpp) and puts one listener on each event through `rex.on`:
fired for an avatar, it runs the handler of the avatar's own class, or of the nearest class above it that has one.
`rex.content.call_parent(Class, event, Self, ...)` runs the next one above `Class`.

#### Variables are each avatar's own

As Blueprint's member variables are, a class graph's variables are each avatar's own, and so are its event dispatchers
and what its nodes keep between runs (a Do Once done, a Delay waiting). They're the fields of a table RexFabric keeps for
the avatar, `rex.content.own(Self, Class, Fresh)`: made by the script's `Fresh` the first time it's asked for, with each
variable's starting value, kept for as long as the avatar is there, and dropped once it's gone (after its removed event).
Every function the script writes, the events', the graph's functions and the chains timers and dispatchers run, takes
the avatar as Self first and fetches its table as `My`, so a variable `Count` is `My.Count`.

An event handed to a timer or a dispatcher is handed on as its chain run for that avatar, the same function each time
(so Unbind finds it), which does nothing once the avatar is gone; a Delay of a removed avatar goes no further either. A
looping timer keeps running until it's cleared, so clear it in On Removed.

`Self`, `My`, `Class` and `Fresh` are the script's own names in a class graph, which variables, functions and
dispatchers can't take.

Here is a graph with a variable `Count`, its On Tick counting ticks and then calling its parent's, and its On Removed
logging the count, with the Lua it saves (its On Spawned left a ghost):

```lua
-- Made with the RexFabric node editor from content/Lights/Glow.graph.json.
-- Saving the graph again replaces this file, so change the graph rather than this file.

-- The class this is the own graph of, as it's named now: wherever its files are, so a rename or a copy needs no compiling.
local Class = rex.content.script_asset()

-- What each avatar's own variables start as; rex.content.own keeps them for it while it's there.
local function Fresh()
  return {
    Count = 0,  -- ticks so far
  }
end

-- On Tick
rex.content.on_class(Class, "avatar_tick", function(Self, n2_Seconds)
  local My = rex.content.own(Self, Class, Fresh)
  My.Count = My.Count + 1
  rex.content.call_parent(Class, "avatar_tick", Self, n2_Seconds)
end)

-- On Removed
rex.content.on_class(Class, "avatar_removed", function(Self)
  local My = rex.content.own(Self, Class, Fresh)
  rex.info("ticks", My.Count)
end)
```

## Files

A mod's content lives in a `content` folder beside its `scripts`, which the browser opens as the mod's root. The
folders inside are whatever the user made:

```
mods/ExampleMod/              the mod's technical root: mod.json, icon.png, scripts
  icon.png
  mod.json
  content/                    the browser's root for ExampleMod; not shown itself
    RandyModel/               a folder the user made
      Randy.json
      Randy.graph.json
      Randy_thumb.png
    Settings/
      RandySettings.json
```

In the browser this is `ExampleMod` holding `RandyModel` and `Settings`, and the class is `ExampleMod/RandyModel/Randy`.

Every asset is a JSON file named after it, so mods diff and merge as text; its name and path come from the file, not
from inside it. `"asset"` says what kind of asset it is, and files of a kind RexFabric doesn't know yet are left
alone. A class asset as stage 1 reads it:

```json
{
  "asset": "class",
  "parent": "ContentBase/Rocks/Rock_Mossy",
  "help": "What it is, for the editor",
  "properties": [
    {"name": "shine", "type": "bool", "default": true, "help": "Whether it glints"}
  ],
  "defaults": {"price": 500}
}
```

`properties` are the ones the class adds: a name, a type (`bool`, `int`, `float`, `string`, `text`, `asset`, `enum` or
`struct`), a default (an empty value of the type when left out) and help. `of` names what the type means: the enum, the
struct, or for an asset the class the asset must be (stage 1 files call that `asset_class`, which still reads).
`container` is `single` (the default), `array` or `map`; a map's `key` is `bool`, `int`, `string`, `enum` or `asset`,
with `key_of` naming its enum. An array is a JSON array, and a map a JSON array of `[key, value]` pairs, so keys of any
type keep their type. An enum value is its name, and a struct an object of its fields. `defaults` changes properties the class inherits, each value checked against the property's type. A
property added twice, a default for a property the parent lacks, a value of the wrong type, a parent that isn't a
class, a parent in a mod that isn't a dependency, or parents in a circle: each is logged with the class's path, and
that class is skipped. Other files in the content folder (pictures, a class's `.graph.json`) belong to the assets
beside them.

An enum and a struct are assets of their own:

```json
{"asset": "enum", "help": "A colour", "values": [{"name": "Red"}, {"name": "Green", "display": "Leaf green"}]}
{"asset": "struct", "fields": [{"name": "x", "type": "float", "default": 1.5}, {"name": "colour", "type": "enum", "of": "MyMod/Colour"}]}
```

A class using them:

```json
{"asset": "class", "properties": [
  {"name": "colours", "type": "enum", "of": "MyMod/Colour", "container": "array", "default": ["Red", "Green"]},
  {"name": "spots", "type": "struct", "of": "MyMod/Spot", "container": "map", "key": "string", "default": [["home", {"x": 3}]]},
  {"name": "weather", "type": "enum", "of": "tip.Weather", "default": "Rain"}
]}
```

A data table names the struct of its rows, then its rows in order, each a `[name, value]` pair, its value a value of
the struct:

```json
{"asset": "table", "row": "MyMod/Spot", "help": "Where things go", "rows": [["Home", {"x": 3}], ["Away", {"colour": "Blue"}]]}
```

A row named with a name that isn't valid, named twice, or that doesn't fit the struct is logged and left out; the rest
of the table loads.

A language asset names the language it's for, then the mod's texts by the game's ids, each with the game's text it was
written over:

```json
{"asset": "language", "language": "englishus", "help": "Sillier menus",
 "texts": {"aid_loctext_pinata_text_xuiscene_frontendpressstart#0a39": {"source": "Press START", "text": "Press START, said the second"}}}
```

One for a language the game doesn't have is logged and left out.

An avatar class names its backing, or leaves it to its parent, and lists the components it adds, as a tree:

```json
{
  "asset": "class",
  "parent": "TiP/Avatar",
  "backing": "TiP/Place",
  "properties": [{"name": "brightness", "type": "int", "default": 2}],
  "components": [
    {
      "name": "Flame",
      "type": "TiP/FxEmitter",
      "transform": {"position": [0, 1.5, 0], "rotation": [0, 90, 0], "scale": [1, 1, 1]},
      "properties": {"emitter": "pinata_misc_candleflame", "every": 0.5},
      "children": [{"name": "Side", "type": "TiP/FxEmitter", "transform": {"position": [1, 0, 0]}, "properties": {"looping": false}}]
    }
  ]
}
```

A transform's position is in the game's units, its rotation pitch, yaw and roll in degrees (turned yaw first), each
part left out when it's the default. A component's `properties` hold only the values it sets; the rest are its type's
defaults. Game classes, backings and component types are named under the game's short name. A graph is the
node editor's file next to its class, compiled to Lua on save as now.

## How a game registers its classes

A game declares its classes before `rexfabric::Start` (`rexfabric/content.h`), after its types, under a path of its own,
each below `Object` or a class it declared before:

```cpp
rexfabric::content::DeclareClass({.path = "TiP/Object", .help = "Any of the game's objects", .type = "tip.Object"});
rexfabric::content::DeclareClass({.path = "TiP/Avatar", .parent = "TiP/Object", .type = "tip.Avatar"});
rexfabric::content::DeclareClass({.path = "TiP/Pinata", .parent = "TiP/Avatar", .test = IsPinata});
```

| Field | Meaning |
|---|---|
| `path` | The class's path, under the game's own folder |
| `parent` | `Object` (the default) or a class declared before it |
| `help` | What it is, for the editor |
| `type` | The game type the class stands for, by its full name. Empty: its objects are of its parent's type |
| `test` | For a class without a type of its own: which objects of its type are one of it. It gets the object as the type holds it, and may throw to refuse |
| `properties` | The properties it adds |
| `events` | Game events its avatars' class graphs can start with (`ClassEventDecl`: the event's full name and its node's title), each declared with `Namespace::Event`, its Args naming the avatar first. The game fires one with the avatar's id first (Class graphs) |

Its structs are declared the same way, after the enums and structs their fields name. Its enums are the ones its API
declares (`Namespace::Enum`), named as Lua names them:

```cpp
using rexfabric::content::PropertyType;
rexfabric::content::DeclareStruct({.path = "TiP/TagRequirement", .help = "One requirement of one tag",
                                   .fields = {{.name = "tag", .type = {.type = PropertyType::kInt}},
                                              {.name = "requirement", .type = {.type = PropertyType::kEnum, .of = "tip.Requirement"}, .value = std::string("Approach")}}});
```

Its tables are declared after the structs of their rows, each with what makes its rows:

```cpp
rexfabric::content::DeclareTable({.path = "TiP/Pinatas", .row = "TiP/Species", .rows = [](rexfabric::content::TableRows& rows) {
  if (!OnGameThread() || !DataLoaded()) return false;  // made later, with FillTable
  for (const Species& species : AllSpecies()) {
    rows.Row(species.name);
    rows.Set("tag", int64_t(species.tag));
    rows.Set("title", species.title);
  }
  return true;
}});
```

A bad name, a path declared twice, an unknown parent, an unknown type or a type another class stands for already is
logged and the class left out. Mods use the game's classes without depending on anything, and a path under one of the
game's folders that no class has is reported as missing rather than as a missing dependency.

Its backings are declared after the class they're for, and its component types after the types their properties name:

```cpp
rexfabric::content::DeclareBacking({.path = "TiP/Place", .for_class = "TiP/Avatar",
                                    .make = MakePlace, .where = PlaceWhere, .move = MovePlace, .remove = RemovePlace});
rexfabric::content::DeclareComponent({.path = "TiP/FxEmitter",
                                      .properties = {{.name = "emitter", .type = {.type = PropertyType::kString}, .value = std::string("pinata_misc_candleflame")},
                                                     {.name = "looping", .type = {.type = PropertyType::kBool}, .value = true}},
                                      .attach = PlayEmitter, .tick = TickEmitter});
```

A backing's functions get the avatar's `Transform` and give a handle of the game's choosing; a component's get a
`ComponentContext`, whose `state` keeps what the component needs between calls (a timer, a handle of its own). Every
one runs on the thread the avatars tick on, and may throw to refuse.

Its worlds, which map edits change (Map edits), are declared before `rexfabric::Start`; it says which it's in each tick
it knows, and may declare kinds of edit of its own:

```cpp
rexfabric::content::DeclareWorld({.path = "TiP/TitleScreen", .help = "The title screen's garden"});
rexfabric::content::SetWorld(on_title_screen ? "TiP/TitleScreen" : "");
rexfabric::content::DeclareMapEditKind({.path = "TiP/ReplaceModel", .transform = false, .properties = {...},
                                        .apply = ReplaceModel, .undo = PutModelBack});
```

Its viewport renderer (The viewport) is registered at any time, after the backings it draws:

```cpp
rexfabric::content::SetViewportRenderer({.show = ShowViewport, .close = CloseViewport});
```

What comes later (editors by class) is a sketch so far; names are tentative:

```cpp
rexfabric::ClassDef& avatar = content.Class("Avatar", "Object").Editor("Avatar");
```

## Lua

`rex.content` reads the classes. Each function follows the dependency rule for the mod calling it: a path in a mod it
doesn't depend on is an error naming the missing dependency.

| Function | Gives |
|---|---|
| `rex.content.classes()` | The path of every class the mod may use: `Object`, the game's, its own and its dependencies' |
| `rex.content.class(path)` | `{path, parent, mod, help}`, or nil for no class; `mod` is empty for RexFabric's and the game's |
| `rex.content.is_a(path, base)` | Whether the class is `base` or comes from it |
| `rex.content.properties(path)` | Every property it has, its ancestors' first: `{name, type, value, declared_by, help}`, `value` the class's own |
| `rex.content.default(path, property)` | The class's value for a property, from itself or its nearest ancestor that sets it |
| `rex.content.cast(value, type)` | The value as an object of a game type or as one of a class, or nil: an object of any type or an address in, the type's object out when it is one |
| `rex.content.valid_name(name)` | Whether a name works, and why not when it doesn't |
| `rex.content.enums()` | Every enum the mod may use: the game's, its own and its dependencies' |
| `rex.content.enum(path)` | `{path, mod, help, values}`, each value `{name, display, help}` and a game enum's `number`; nil for none |
| `rex.content.structs()` | Every struct the mod may use |
| `rex.content.struct(path)` | `{path, mod, help, fields}`, each field as `properties` gives a property; nil for none |
| `rex.content.tables()` | Every data table the mod may use: the game's, its own and its dependencies' |
| `rex.content.table(path)` | `{path, mod, help, row, game, names}`: its row struct, whether it's the game's, and its rows' names in order |
| `rex.content.row(table, name)` | The row as its struct, or nil |
| `rex.content.row_names(table)` | Every row's name, in order |
| `rex.content.has_row(table, name)` | Whether it has the row |
| `rex.content.spawn(class, x, y, z, yaw, options)` | A new avatar of an avatar class there, turned `yaw` degrees (0 when left out); an error naming why it can't be. `options.saved` keeps it in its world's save (Saved with the world) |
| `rex.content.remove(avatar)` | Removes it: true when it was there |
| `rex.content.is_avatar(value)` | Whether it's an avatar still there |
| `rex.content.avatars(class)` | Every avatar of the class or a class below it, every avatar when left out, oldest first |
| `rex.content.avatar(value)` | The avatar a value stands for, an avatar or its id, while it's there; nil for anything else |
| `rex.content.on_class(class, event, fn)` | `fn(avatar, ...)` runs when an avatar event is fired for an avatar of the class or a class below it, unless a class between them has its own handler of the event (Class graphs) |
| `rex.content.call_parent(class, event, avatar, ...)` | The handler of the event of the nearest class above `class` that has one; nothing for none |
| `rex.content.own(avatar, key, make)` | The avatar's own table under `key`, made by `make()` (or empty) the first time, kept while the avatar is there and dropped once it's gone |

An avatar has `id`, `class`, `valid`, `x`, `y`, `z` and `yaw` (setting one of the last four moves it), `object` (its
backing's game object, nil for a place), `saved` (set it to keep the avatar in its world's save or not), and `:move(x, y, z, yaw)`, `:get(property)`, `:set(property, value)` and
`:remove()`. Using one that's gone is an error, so ask `valid` first.

Every text these give is in the language the game shows, when the mod has a translation of it (Localisation).

Values come as their types say: an array as a list, a map as a table under its keys, a struct as a table of its fields
(a field missing from the value as its default), a mod enum's value as its name and a game enum's as its number, as
the game's functions take it. `properties` and `struct` give each one's `type`, `of`, `container`, and for a map
`key` and `key_of`.

```lua
rex.content.default("ContentChild/Golden_Rock", "price")                 -- 500, its own default
rex.content.default("ContentChild/Golden_Rock", "display_name")          -- "Mossy Rock", from Rock_Mossy
rex.content.is_a("ContentChild/Golden_Rock", "ContentBase/Rocks/Rock_Base")  -- true
local pinata = rex.content.cast(avatar, "TiP/Pinata")                     -- the avatar as a pinata, or nil
```

A cast to a mod's class gives the avatar of it whose backing is the object, or nil. The Cast To nodes of game classes
compile to `cast`.

Tested with four mods: a base mod with a class and a subclass of it; a mod depending on it that subclassed the
subclass and read it from Lua (the inherited and changed defaults, `is_a` up to `Object`); a mod using the base mod's
class without the dependency, whose class was refused and whose Lua got the error; and a mod depending on a mod that
isn't installed, which didn't load. A file named `Bad Name.json` was skipped with the reason.

Stage 2's Lua was tested in a garden with Trouble in Paradise's classes: the class list (`Object`, `TiP/Object`,
`TiP/Avatar`, `TiP/Pinata`); every pinata (12) cast to `TiP/Pinata` and none of the 229 other avatars; a pinata cast up
to `tip.Object` and back down; a cast by address; nil and a string giving nil; an unknown `TiP/` class reported as
missing; a mod class below `TiP/Pinata` being an avatar, with its own default, and a pinata not being one of it; and a
mod needing a newer game version refused with both versions named. The node editor's type pickers were built but not
yet tried by hand.

The browser and editors were tested in Trouble in Paradise's title screen: a class of a mod that's off opened, changed
and saved (with the status saying it doesn't load); a new mod made, with a class below `TiP/Pinata` that loaded at once
(the log's class count went up); a subclass from the tile's menu; a rename and a drag into a folder, each repointing the
subclass's parent; a property added to the parent showing in the subclass's editor after the save, and set there; a
dependency added in the metadata editor, the mod then reported as not loading while it was off; a class whose parent is
in a mod that isn't a dependency, fixed with its Add as a dependency; a duplicate; a delete listing the two classes
naming it; search and the class filter across every mod; and closing with an unsaved class asking first.

Stage 3 was tested on the title screen with a test mod holding an enum, a struct of floats and the enum, and a class
with an array of the enum, a map of strings to the struct, a game enum, an array of the game's `TiP/TagRequirement`
and a map keyed by the enum. From Lua: both listed with the game's, the struct's missing fields given as their
defaults, the game enum's values as its numbers (Rain 2, Mate 3), and a class whose default isn't one of its enum's
values refused with the reason. A graph with variables of each (an array of the enum, the struct, a game enum, a map
keyed by the enum), looping the array through the enum's text, breaking the struct, finding in the map by an enum value
picked on its node, and making and breaking the game's struct, compiled with no problems and logged the right values.
In the editors: the array opened as a tree with its values picked by display name, the variable's details showed its
enum, container and starting array, and renaming an enum value and saving renamed it in the struct holding it.

Stage 4 was tested on the title screen. A mod's table of its struct loaded with its two good rows, the third (named
`bad name!`) logged and left out, and read from Lua with its missing fields as their defaults. The game's four tables
were empty when read before the title screen's garden loaded, then read in `tip.garden_loaded`: 119 species, 618
items with their prices and unlock levels, the 618 rows of the shops' unlock table, and every species' requirements
in words, with the randomizer's marked as a mod's. Setting an item's price and reading its row again gave the new
price, and resetting it the old. A graph's For Each Row listed the table's rows and Get Row found one and broke its
struct. In the browser, Game Tables listed the four, Pinatas opened as a grid, and the mod's table opened in its
editor with its bad row name reported.

Stage 5 was tested on the title screen, the game in American English, with the test mod written in French and an
American English string table: a class's text property, an enum's display name, a table row's text and a row leaving
its text to the struct's default each came to Lua in English; a text whose translation was made from an older source
came in its old translation; an enum value with no translation came as written. The translation editor listed the
seven texts with two missing, one out of date and four done.

The game's texts were tested the same way: the game listed 15978 texts in 3960 loctext assets in about a tenth of a
second, the editor searched them, and the title screen's Press START, changed in a language asset of American English
and saved, showed the mod's text the next time the game started. Its label, `text__button_PressStart`, was learned
from the game's lookup; a text the game hadn't looked up showed its id.

Textures were tested on the title screen: a PNG imported through the Open dialog into the signposts test mod came in
as `MenuSigns/ModLogo`, its picture copied beside it, shown on its tile and in its editor; the signpost class's
`texture` property picked it from a list of the mod's textures alone, and the sign showed it on its board.

The viewport was tested on the title screen, in the native frame. The signposts test mod's Hello sign drew in its
editor's viewport as the game's blue signpost with the mod's picture on its board, lit as the journal lights its
avatars. Dragging went round it and the wheel came nearer. Setting its colour to Orange drew the orange sign at once,
unsaved. A component added to it showed its marker at the sign's foot. Its gizmo moved it up the post along y alone, to
(0, 20.5465, 0), and one undo took the whole drag back. A `TiP/Place` class showed the viewport's own camera over a grid,
its inherited components grey and its own orange. Clicking its own marker selected it in the tree too, and its gizmo
turned it to a pitch of 35.8881 with nothing else changed. Closing the editors let the game's viewer go. Then, with the
game drawing its own stage: a component added to the sign played its candle flame, hidden by the post at the sign's
foot and shown in full when moved out in front, the grid under the sign and the flame; Pause held them still and Play
ran them on; the place class's three flames showed with no model at all, the framing taking them in.

Stage 6 was tested on the title screen with a mod of three classes below `TiP/Avatar`: one with an fx emitter at 1.5
up looping every half second and a child emitter 1 to the side playing once; a subclass adding a third and changing a
default; and one naming a component as an inherited one does, refused with the reason. From Lua: a glow spawned at
every signpost played its flames there, each offset turned with its sign's yaw; `get` gave the class's value and
`set` changed the avatar's alone; casts to its own class gave it, to `TiP/Avatar` and `tip.Avatar` nil (a place has
no object); `avatars` counted the subclass's with its parent's; moving it moved its flames; removing it made `valid`
false and reading it an error; spawning `TiP/Avatar` itself was refused; and the attract sequence's change of garden
dropped every avatar. In the class editor the Avatar section showed the inherited backing, the tree with its
transforms and values, and a reset value saved back to the file. A graph of On Garden Loaded, Spawn Glow_Big and
Avatar Location compiled with no problems, spawned it and logged its X.

## Runtime

When a mod is enabled its class assets load, parents first, and their graphs run with the mod's scripts. Spawning an
avatar class makes its backing in the world, attaches its components in tree order and fires its spawned event, which
its class graph runs; each frame its components tick and its tick event fires; removing it fires its removed event,
then detaches its components in reverse order (Avatars, What is built; Class graphs).

Mod avatars are never written into the game's own save. The ones spawned saved are kept beside it as records of their
class, place and values, and spawned again as the same class when the world loads; a record whose class is gone is
kept, not spawned, and a save never fails to load for it (Saved with the world).

## Build stages

Stages 7 and 8 were built after stage 2, so the later stages each land in an editor that exists. Stages 3 and 4 came
next, then stage 5 but for adding languages, then stage 6.

1. The class model and its files, without an editor: `Object`, properties, inheritance, class assets loaded from mods
   and spawned from Lua; the mod metadata in `mod.toml` with dependencies and the minimum game version, and the
   dependency rule for references.
2. Class types: the game's fundamental types registered through its API, mod classes as types, typed pins and
   variables in the node editor, and the cast node.
3. Structs, enums and containers: mod structs and enums as assets, the game's declared ones, and arrays and maps on
   every property and variable.
4. Data tables: mod tables and their grid editor, the table nodes, and the game's read only tables (for Trouble in
   Paradise, pinatas, items, shop items and requirements).
5. Localisation: string tables for mods' texts, the translation editor, then the game's localisation editor and
   language assets.
6. The game's `Avatar`, one backing (a plain world object) and the first component, `pinata_fx_emitter`.
7. The content browser.
8. The properties editor, which every class can already use.
9. The Avatar editor: the component tree and defaults (built), then the graph (built: Class graphs), then the viewport
   (for Trouble in Paradise, the journal viewer drawing into a texture) and ImGuizmo's gizmos over it (built).
10. Game-backed avatars for Trouble in Paradise: decor and pinata classes as new items of the game (new tags, new
    assets and models; see DeTiP's notes on asset loading).
11. Asset classes and their editors: `Texture` and a texture viewer (built), then `Model`.

## Later

* Multiplayer: everyone in a session having the same mods and versions, and which machine runs a mod avatar's graph.
  Left until the rest stands.
* A version in every asset file, so older mods are brought up to date as they load when the format or a game's API
  changes.
* Values set on one placed instance rather than its class, and where placements live: in a save, or in the mod. A map
  edit's spawns are placements living in the mod, with values of their own; placements in a save are still open.
* What a rename leaves behind for mods that depend on the old path.

## Open questions

* Whether content edits apply live, or on the next spawn of the class.
* How undo works across the browser and the editors.
* How two mods that subclass the same game class without knowing of each other stay independent.
* Whether display names are `text` everywhere or only on classes the player sees.
