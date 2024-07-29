# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(bc-write) begin
(bc-write) Create file "test".
(bc-write) Open file "test".
(bc-write) Write >= 64KiB to file.
(bc-write) File has size >= 64KiB.
(bc-write) Read >= 64KiB from file.
(bc-write) The total number of device writes is on order of 128.
(bc-write) end
bc-write: exit(0)
EOF
pass;
