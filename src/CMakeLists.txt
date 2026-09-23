# The own-code root. Everything we write lives below here and nothing else does, which makes
# external/ a sibling rather than a descendant: a directory property set here reaches every
# target of ours and no submodule, whatever order the top level adds them in.

add_subdirectory(rke)

add_subdirectory(benchmark)
