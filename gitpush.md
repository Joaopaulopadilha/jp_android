git add .
git commit -m "JPLang 3.0"
git push origin main



git tag -d v3.0
git push origin --delete v3.0
git tag v3.0 -m "Versão 3.0 - atualizada"
git push origin v3.0


essa é uma biblioteca da minha linguagem, use ela como exemplo para elaboracao 