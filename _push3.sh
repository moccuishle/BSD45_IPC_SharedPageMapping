#!/usr/bin/env bash
export PATH=/usr/bin:/c/msys64/usr/bin:$PATH
cd /c/Users/Administrator/Desktop/work || exit 1
git add -A
git commit -q -m "docs: record the S17 cache modules in the kernel-boundary inventory" || exit 0
export GIT_SSH_COMMAND="/c/Windows/System32/OpenSSH/ssh.exe -o StrictHostKeyChecking=no"
git push origin main 2>&1 | tail -8
git log --oneline -2
