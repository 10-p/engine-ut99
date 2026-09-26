# layout-sweep.gdb -- every script property of every native class vs the same-named C++ member.
#
#   cd <run-tree>/System && gdb -batch -x <engine-repo>/tools/dev/layout-sweep.gdb --args ./ucc-bin make -nohomedir
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
#     checked=<n> shifted=1
# Needs a build with debug info (the native preset is RelWithDebInfo).
set pagination off
set debuginfod enabled off
break UEditorEngine::Destroy
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
    for f in ctype.fields():
        if f.is_base_class or not hasattr(f, 'bitpos'):
            continue
        if f.bitsize:
            members[f.name.lower()] = ((f.bitpos // 32) * 4, 1 << (f.bitpos % 32))
        else:
            members[f.name.lower()] = (f.bitpos // 8, 0)
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
            if cpp[0] != off or (pclass == 'BoolProperty' and cpp[1] and cpp[1] != mask):
                shifted += 1
                print("SHIFT %s.%s (%s) C++=%d/%x script=%d/%x" % (cname, name_of(obj), pclass, cpp[0], cpp[1], off, mask))
        field = field['Next']
print("checked=%d shifted=%d" % (checked, shifted))
end
kill
quit
