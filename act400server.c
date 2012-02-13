/*
* Serveur de socket pour OS400
* Lancer le serveur avec la commande : ./act400server 4443 192.168.3.30 2<&1
* @author Yann GAUTHERON <ygautheron@absystech.fr>
* @date 2012-02-03
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

/* Execute une commande OS400
* @author Yann GAUTHERON <ygautheron@absystech.fr>
* @param char *cmd Syntaxe à exécuter
* @param char *result Résultat de la commande
* @param int result_size Taille du résultat
* @param char *transform Transformation de la syntaxe, par exemple "SYSTEM \"ACTCTL/%s\""
* @return int 0 si la commande s'est bien exécutée, -1 si le code d'erreur n'est pas trouvé en prefixe du retour standard STDIN
*/
int execOS400(char *cmd, char *result, int result_size, char *transform) {
	char buf[2048]; // variable temporaire
    FILE* fp; // Descripteur de lecture de l'exécution de la commande
//	bzero(buf,sizeof(buf)); // Clean du buffer
	bzero(result,result_size); // Clean du result
	
	snprintf(buf, sizeof buf, transform, cmd);
	printf("Transformation de la commande en : %s\n",buf);
	
	// Execution de la commande
	fp = popen(buf,"r");
	fread(result,1,result_size,fp);
	fclose(fp);
	printf("La commande a retourne :\n%s",result);
	
	bzero(buf,sizeof(buf));
	strncpy(buf, (char *)result, 8);
	if (strcmp(buf,"CPD0030:")==0) {
		return -1;
	} else {
		return 0;
	}
}

/* Prédicat qui analyse si la chaine recherchée est trouvée dans la chaine source séparée par virgule
* @author Yann GAUTHERON <ygautheron@absystech.fr>
* @param char *source Chaine à analyser, composée de sous chaines séparées par virgules
* @param char *search Chaine recherchée
* @return int 0 si trouvé, -1 sinon
*/
int checkIP(char *source, char *search) {
	char tmp[strlen(source)+1]; // variable temporaire
	bzero(tmp,sizeof(source)); // Clean du result
	strcpy(tmp,source);
	char *p;
	p = strtok (tmp, ",");
	while (p != NULL){
//printf ("Chaine trouvee : %s\n", p);
		if (strcmp(p,search)==0) {
			return 0;
		}
		p = strtok (NULL, ",");
	}
	return -1;
}

/* Supprime les espaces qui entourent une chaine
* @author Yann GAUTHERON <ygautheron@absystech.fr>
* @param char *str Chaine à modifier
*/
char *trim(char *str){
	char *end;

	// Trim leading space
	while(isspace(*str)) {
		str++;
	}

	if(*str == 0)  // All spaces?
	return str;

	// Trim trailing space
	end = str + strlen(str) - 1;
	while(end > str && isspace(*end)) end--;

	// Write new null terminator
	*(end+1) = 0;

	return str;
}

/*
* Procédure d'erreur standard
* @author Yann GAUTHERON <ygautheron@absystech.fr>
* @param char *msg Message d'erreur
*/
void error(const char *msg) {
	perror(msg);
	exit(1);
}

/*
* Programme de serveur de socket
* @author Yann GAUTHERON <ygautheron@absystech.fr>
*/
int main(int argc, char *argv[]) {
	const char* transformPrimary = "system \"ACTCTL/%s\"";
	const char* transformSecondary = "system \"ACTCTLSPE/%s\"";
	const char* transformForced = "system \"%s\"";
	
	int sockfd, newsockfd, portno,cli_portno;
	socklen_t clilen;
	char buffer[2048];
	struct sockaddr_in serv_addr, cli_addr;
	char ipstr[INET6_ADDRSTRLEN];
	int n;
    char result[42000]; // Buffer de résultat de la commande
	if (argc < 2) {
		fprintf(stderr,"ERREUR, aucun port fourni en parametre. Usage : %s port [ip_autorisee1[,ip_autorisee2[...]]]\n",argv[0]);
		exit(1);
	}
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		error("ERREUR ouverture de socket");
	}
	bzero((char *) &serv_addr, sizeof(serv_addr));
	portno = atoi(argv[1]);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(portno);
	printf("Socket cree sur le port %i.\n",portno);
	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		error("ERREUR bind socket");
	}
	listen(sockfd,5);
	clilen = sizeof(cli_addr);
	while (1) {
		printf("------- Socket en attente d'acceptation...\n");
		newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);

		// signal(SIGCHLD, SIG_IGN);
		// switch( childpid=fork()){
		// case -1://error
			// fprintf(stderr, "Error spawning the child %d\n",errno);
			// exit(0);
		// case 0://in the child
			// SocketHandler(csock);
			// exit(0);
		// default://in the server
			// close(*csock);
			// free(csock);
		// }		
				
		
		if (newsockfd < 0) {
			error("ERREUR accept socket");
		}
		bzero(buffer,sizeof(buffer)); // Clean du buffer
		
		// Réception du client
		getpeername(newsockfd, (struct sockaddr*)&cli_addr, &clilen);	
		struct sockaddr_in *s = (struct sockaddr_in *)&cli_addr;
		cli_portno = ntohs(s->sin_port);
		inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
		
		printf("Socket client connecte, en attente de sa commande %s:%i.\n",ipstr,cli_portno);
		
		// Vérification de l'IP acceptée
		if (argc<3 || checkIP(argv[2],ipstr)==0) {
		//if (argc<3 || strcmp(argv[2],ipstr)==0) {
			// Lecture du message
			n = read(newsockfd,buffer,sizeof(buffer)-1);
			if (n < 0) {
				error("ERREUR lecture depuis socket");
			}
//printf("Avant trim :|%s|\n",buffer);
			strcpy(buffer, trim(buffer));
//printf("Apres trim :|%s|\n",buffer);
//			printf("Reception de la commande : %s\n",buffer);

			if (strstr(buffer,"ACTCTLSPE/")!=NULL) {
				// Prefixe forcé
				printf("Prefixe forced\n");
				execOS400(buffer, (char *)&result, sizeof(result), (char *)transformForced);
			} else {
				// Si la commande échoue, on change le prefixe en ACTCTLSPE/
				if (execOS400(buffer, (char *)&result, sizeof(result), (char *)transformPrimary)!=0) {
					//printf("La commande ne semble pas exister dans la bibliotheque primaire (%s), essai dans la bibliotheque secondaire...\n","CPD0030:",result);
					execOS400(buffer, (char *)&result, sizeof(result), (char *)transformSecondary);
				}
			}
			
			// Envoi au client
			printf("Envoi du resultat au client.\n");
			n = write(newsockfd,result,sizeof(result));
			if (n < 0) {
				error("ERREUR ecriture vers socket");
			}	
		} else {
			fprintf(stderr,"ERREUR, adresse IP non autorisee (differente de %s).\n",argv[2]);
		}
		close(newsockfd);
		printf("Connexion fermee.\n");
		
		// Terminer le fils
		printf("Attente 5 secondes\n");
		sleep(5);

	}
	close(sockfd);
	return 0; 
}