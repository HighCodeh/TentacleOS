#ifndef SUBGHZ_FILE_LOADER_H
#define SUBGHZ_FILE_LOADER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Percorre um diretório procurando arquivos .sub e tenta identificar o protocolo
 * 
 * @param dir_path Caminho do diretório (ex: "/assets/tmp")
 */
void subghz_loader_process_directory(const char* dir_path);

#ifdef __cplusplus
}
#endif

#endif // SUBGHZ_FILE_LOADER_H
