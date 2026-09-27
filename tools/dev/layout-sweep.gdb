# layout-sweep.gdb -- every script property of every native class vs the same-named C++ member.
#
#   cd <run-tree>/System && gdb -batch -x <engine-repo>/tools/dev/layout-sweep.gdb --args ./ucc-bin make -nohomedir
#   cd <run-tree>/System && gdb -batch -x <engine-repo>/tools/dev/layout-sweep.gdb --args ./UnrealTournament -SOFTWARERENDERER
#
# Run BOTH hosts (ufront 2.83). Some reflected layouts exist only in one process: the input alias struct is
# built by UInput::StaticInitInput, which only a client (or the editor) calls, so `ucc make` never saw it and
# its 64-bit layout crashed the first key bound to an alias. The client host stops at the first game tick
# (map loaded, viewport open); headless it runs under Xvfb (any render device will do).
#
# (ufront 2.80) The runtime layout guard in UStruct::Link compares only a native class's C++ sizeof with
# its script size. It cannot see a member that moved inside a class when a later one re-aligns, and it
# stayed silent on the EditorEngine shift that crashed every `ucc make`. This compares OFFSETS, property
# by property: UE1 loads a native class's script properties at the offsets UStruct::Link computes (the
# MSVC layout the scripts were compiled against), while the C++ reads its members at GCC/clang's offsets.
# Bools are compared as (32-bit unit, bit). The two compilers' known disagreements -- GCC reusing a base's
# tail padding, packing a BYTE into a bitfield unit's spare bytes or a bitfield into bytes' unit -- are
# fixed in the headers with GCC_ALIGN(4) / GCC_PACK(4).
#
# `ucc make` loads every EditPackages entry, i.e. every package with native classes; the sweep runs when
# the editor engine is destroyed at exit. Expected result on a healthy tree:
#     SHIFT EditorEngine.Pad (IntProperty) ...     <- a reserved placeholder on both sides, never read
#     checked=<n> shifted=1                        (the client host: shifted=0; it has no editor)
#     structs=<n> checked=<n> shifted=0     <- script structs vs same-named C++ F<Name> (ufront 2.83)
# A property may point at a same-named member of a nested struct rather than the class's own member
# (UTGLR registers DCV.SinglePassFog and keeps a runtime copy SinglePassFog beside it): one level of
# nested members is accepted as a match.
# Needs a build with debug info (the native preset is RelWithDebInfo).
set pagination off
set debuginfod enabled off
python
import gdb, os
host = os.path.basename(gdb.current_progspace().filename)
gdb.execute("break " + ("UEditorEngine::Destroy" if host.startswith("ucc") else "UGameEngine::Tick"))
end
run
python
import gdb
def name_of(obj):
    idx = int(obj['Name']['Index'])
    return gdb.parse_and_eval("((FNameEntry**)FName::Names.Data)[%d]->Name" % idx).string()
UObjectPtr   = gdb.lookup_type('UObject').pointer()
UPropertyPtr = gdb.lookup_type('UProperty').pointer()
UBoolPtr     = gdb.lookup_type('UBoolProperty').pointer()
count = int(gdb.parse_and_eval("GLayoutCount"))
checked = shifted = 0
for i in range(count):
    struct = gdb.parse_and_eval("GLayoutStructs[%d]" % i)
    cname  = name_of(struct)
    ctype  = None
    for prefix in ('A', 'U', 'F', ''):
        try:
            ctype = gdb.lookup_type(prefix + cname); break
        except gdb.error:
            pass
    if ctype is None:
        print("NOTYPE %s" % cname); continue
    members = {}
    nested  = {}
    for f in ctype.fields():
        if f.is_base_class or not hasattr(f, 'bitpos'):
            continue
        if f.bitsize:
            members[f.name.lower()] = ((f.bitpos // 32) * 4, 1 << (f.bitpos % 32))
        else:
            members[f.name.lower()] = (f.bitpos // 8, 0)
            ftype = f.type.strip_typedefs()
            if ftype.code == gdb.TYPE_CODE_STRUCT:
                for g in ftype.fields():
                    if not g.is_base_class and hasattr(g, 'bitpos') and not g.bitsize and g.name:
                        nested.setdefault(g.name.lower(), set()).add((f.bitpos + g.bitpos) // 8)
    size_cpp = int(gdb.parse_and_eval("GLayoutSizes[%d]" % i))
    if int(struct['PropertiesSize']) != size_cpp:
        print("SIZE %s C++=%d script=%d" % (cname, size_cpp, int(struct['PropertiesSize'])))
    field = struct['Children']
    while int(field) != 0:
        obj = field.cast(UObjectPtr)
        if int(obj['Outer']) != int(struct.cast(UObjectPtr)):
            break
        pclass = name_of(obj['Class'])
        cpp = members.get(name_of(obj).lower())
        if pclass.endswith('Property') and cpp is not None:
            checked += 1
            off  = int(field.cast(UPropertyPtr)['Offset'])
            mask = int(field.cast(UBoolPtr)['BitMask']) if pclass == 'BoolProperty' else 0
            if (cpp[0] != off or (pclass == 'BoolProperty' and cpp[1] and cpp[1] != mask)) \
                    and off not in nested.get(name_of(obj).lower(), ()):
                shifted += 1
                print("SHIFT %s.%s (%s) C++=%d/%x script=%d/%x" % (cname, name_of(obj), pclass, cpp[0], cpp[1], off, mask))
        field = field['Next']
print("checked=%d shifted=%d" % (checked, shifted))

# (ufront 2.83) Script STRUCTS mirrored by a C++ struct F<Name>. The runtime guard only knows native
# classes, but a struct's size is also a stride (TArray<FBuilderPoly>) and its members are read by C++ at
# its own offsets, so every script struct with a same-named C++ type is compared too: size, then each
# property. A name can match an unrelated C++ type -- read a report before acting on it.
UStructPtr = gdb.lookup_type('UStruct').pointer()
objs = gdb.parse_and_eval("UObject::GObjObjects")
data = objs['Data'].cast(UObjectPtr.pointer())
s_checked = s_shifted = s_structs = 0
for k in range(int(objs['ArrayNum'])):
    o = data[k]
    if int(o) == 0 or name_of(o['Class']) != 'Struct':
        continue
    sname = name_of(o)
    try:
        ctype = gdb.lookup_type('F' + sname).strip_typedefs()
    except gdb.error:
        continue
    if ctype.code != gdb.TYPE_CODE_STRUCT:
        continue
    s_structs += 1
    st = o.cast(UStructPtr)
    outer = name_of(o['Outer'])
    if int(st['PropertiesSize']) != ctype.sizeof:
        print("SSIZE %s.%s C++ F%s=%d script=%d" % (outer, sname, sname, ctype.sizeof, int(st['PropertiesSize'])))
    members = {}
    for f in ctype.fields():
        if f.is_base_class or not hasattr(f, 'bitpos'):
            continue
        members[f.name.lower()] = f.bitpos // 8
    field = st['Children']
    while int(field) != 0:
        fo = field.cast(UObjectPtr)
        if int(fo['Outer']) != int(o):
            break
        pclass = name_of(fo['Class'])
        cpp = members.get(name_of(fo).lower())
        if pclass.endswith('Property') and cpp is not None:
            s_checked += 1
            off = int(field.cast(UPropertyPtr)['Offset'])
            if cpp != off:
                s_shifted += 1
                print("SSHIFT %s.%s.%s (%s) C++=%d script=%d" % (outer, sname, name_of(fo), pclass, cpp, off))
        field = field['Next']
print("structs=%d checked=%d shifted=%d" % (s_structs, s_checked, s_shifted))
end
kill
quit
