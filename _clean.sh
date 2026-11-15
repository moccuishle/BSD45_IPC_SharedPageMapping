#!/usr/bin/env bash
export PATH=/usr/bin:/c/msys64/usr/bin:$PATH
cd /c/Users/Administrator/Desktop/work || exit 1
printf '_src.tar.gz\n_tools/\n_chk.sh\n' >> .git/info/exclude
git rm -q --cached _push3.sh 2>/dev/null
rm -f _push3.sh
git checkout -- . 2>/dev/null
git add -A
git commit -q -m "cleanup: drop a stray temp script from the tree" || true
git status --short | head -5
echo "--- log ---"
git log --oneline -3
