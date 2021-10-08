#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <commun.h>
#include <liste.h>
#include <piste.h>

static struct sembuf Op_P = {0,-1,0};
static struct sembuf Op_V = {0,1,0};

void P(int id, int i){
    Op_P.sem_num = i;
    semop(id, &Op_P, 1);
}

void V(int id, int i){
    Op_V.sem_num = i;
    semop(id, &Op_V, 1);
}

int
main( int nb_arg , char * tab_arg[] ){

    int cle_piste ;
    piste_t * piste = NULL ;

    int cle_liste ;
    liste_t * liste = NULL ;

    char marque ;

    booleen_t fini = FAUX ;
    piste_id_t deplacement = 0 ;
    piste_id_t depart = 0 ;
    piste_id_t arrivee = 0 ;


    cell_t cell_cheval ;


    elem_t elem_cheval ;


    int pos_cheval;
    int shmid_liste, shmid_piste;
    int semid_liste, semid_piste;


    /*-----*/

    if( nb_arg != 4 )
    {
        fprintf( stderr, "usage : %s <cle de piste> <cle de liste> <marque>\n" , tab_arg[0] );
        exit(-1);
    }

    if( sscanf( tab_arg[1] , "%d" , &cle_piste) != 1 )
    {
        fprintf( stderr, "%s : erreur , mauvaise cle de piste (%s)\n" ,
                 tab_arg[0]  , tab_arg[1] );
        exit(-2);
    }


    if( sscanf( tab_arg[2] , "%d" , &cle_liste) != 1 )
    {
        fprintf( stderr, "%s : erreur , mauvaise cle de liste (%s)\n" ,
                 tab_arg[0]  , tab_arg[2] );
        exit(-2);
    }

    if( sscanf( tab_arg[3] , "%c" , &marque) != 1 )
    {
        fprintf( stderr, "%s : erreur , mauvaise marque de cheval (%s)\n" ,
                 tab_arg[0]  , tab_arg[3] );
        exit(-2);
    }

    /* Récupération des id de mémoire partagée pour la liste */
    if((shmid_liste = shmget(cle_liste, LISTE_MAX*sizeof(elem_t), IPC_CREAT | 0666)) == -1){
        perror("shmid_liste");
        exit(-3);
    }
    if((liste = shmat(shmid_liste, 0, 0)) == ((liste_t*)-1)){
        perror("liste");
        exit(-4);
    }

    /* Création sémaphore liste */
    if((semid_liste = semget(cle_liste, 1, 0666)) == -1){
        perror("semid_liste");
        exit(-1);
    }


    /* Récupération des id de mémoire partagée pour la piste */
    if((shmid_piste = shmget(cle_piste, PISTE_LONGUEUR*sizeof(cell_t), IPC_CREAT | 0666)) == -1){
        perror("shmid_piste");
        exit(-3);
    }
    if((piste = shmat(shmid_piste, 0, 0)) == ((piste_t *)-1)){
        perror("piste");
        exit(-4);
    }

    /* Création d'un sémaphore par cellules sur la piste */
    if((semid_piste = semget(cle_piste, PISTE_LONGUEUR, 0666)) == -1){
        perror("semid_piste");
        exit(-1);
    }

    /* Init de l'attente */
    commun_initialiser_attentes() ;

    /* Init de la cellule du cheval pour faire la course */
    cell_pid_affecter( &cell_cheval  , getpid());
    cell_marque_affecter( &cell_cheval , marque );

    /* Init de l'element du cheval pour l'enregistrement */
    elem_cell_affecter(&elem_cheval , cell_cheval ) ;
    elem_etat_affecter(&elem_cheval , EN_COURSE ) ;

    /* Création du sémaphore cheval */
    if(elem_sem_creer(&elem_cheval) == -1){
        perror("elem_sem_creer");
        exit(-1);
    }

    /*
    * Enregistrement du cheval dans la liste
    */

    /* Pose du sémaphore sur liste */
    semop(semid_liste, &Op_P,1);

    /* Ajout du cheval dans la liste */
    if((liste_elem_ajouter(liste, elem_cheval)) != 0){
        perror("liste_elem_ajouter");
        exit(-1);
    }

    /* Leve du sémaphore sur liste */
    semop(semid_liste, &Op_V,1);

    /* Position du cheval ajouter */
    if(liste_elem_rechercher(&pos_cheval, liste, elem_cheval) == FAUX){
        perror("liste_elem_rechercher\n");
        exit(-1);
    }

    while( ! fini ){
        /* Attente entre 2 coup de de */
        commun_attendre_tour() ;
        /*
         * Verif si pas decanille
         */

        /* Pose du sémaphore sur liste */
        semop(semid_liste, &Op_P,1);

        /* Si le cheval est décanillé --> raz état cellule piste && delete liste */
        if(elem_decanille(elem_cheval)){
            /* Pose du sémaphore sur piste */
            semop(semid_piste, &Op_P,1);

            /* Efface la case ou se situe le cheval */
            piste_cell_effacer(piste, pos_cheval);

            /* Leve du sémaphore sur piste */
            semop(semid_piste, &Op_V,1);

            /* Position du cheval a supprimer */
            if(liste_elem_rechercher(&pos_cheval, liste, elem_cheval) == FAUX){
                perror("liste_elem_rechercher\n");
                exit(-1);
            }

            /* Suppression du cheval de la liste */
            if(liste_elem_supprimer(liste, pos_cheval) != 0){
                perror("liste_elem_supprimer");
                exit(-1);
            }

            /* Suppression du cheval */
            if(elem_sem_detruire(&elem_cheval) != 0){
                perror("elem_sem_detruire");
                exit(-1);
            }

            exit(0);
        }

        /* Leve du sémaphore sur liste */
        semop(semid_liste, &Op_V,1);

        /*
         * Avancee sur la piste
         */

        /* Coup de de */
        deplacement = commun_coup_de_de() ;

#ifdef _DEBUG_
        printf(" Lancement du De --> %d\n", deplacement );
#endif

        arrivee = depart+deplacement ;

        if( arrivee > PISTE_LONGUEUR-1 ){
            arrivee = PISTE_LONGUEUR-1 ;
            fini = VRAI ;
        }

        if( depart != arrivee ){
            /* Pose sémaphore sur cheval */
            if(elem_sem_verrouiller(&elem_cheval) == -1){
                perror("elem_sem_verrouiller");
                exit(-1);
            }

            /* Décanille si case occupé */
            if(piste_cell_occupee(piste, arrivee)){
                elem_t cheval;
                cell_t posCheval;
                int idCheval;

                /* Récupération de la case d'arrivé */
                if(piste_cell_lire(piste, arrivee, &posCheval) != 1){
                    perror("piste_cell_lire");
                    exit(-1);
                }

                /* Position du cheval a décanillé */
                elem_cell_affecter(&cheval, posCheval);

                /* Pose sémaphore sur liste */
                semop(semid_liste,&Op_P,1);

                /* Position du cheval a décanillé */
                if(liste_elem_rechercher(&idCheval, liste, cheval) == FAUX){
                    perror("liste_elem_rechercher\n");
                    exit(-1);
                }

                /* Pose sémaphore cheval */
                if(elem_sem_verrouiller(&cheval) == -1){
                    perror("elem_sem_verrouiller");
                    exit(-1);
                }

                /* Décanillage du cheval */
                if(liste_elem_decaniller(liste, idCheval) == -1){
                    perror("liste_elem_decaniller\n" );
                    exit(-1);
                }

                /* Leve semaphore cheval */
                if(elem_sem_deverrouiller(&cheval) == -1){
                    perror("elem_sem_deverrouiller");
                    exit(-1);
                }

                /* Leve semaphore liste */
                semop(semid_liste, &Op_V,1);
            }

            /* Pose sémaphore sur cell depart */
            P(semid_piste, depart);

            /* Delete du cheval sur sa case */
            piste_cell_effacer(piste, depart);

            /* Leve sémaphore sur cell depart */
            V(semid_piste, depart);

            /* Saut */
            commun_attendre_fin_saut();

            /* Traitement sur le cheval fini */
            if(elem_sem_deverrouiller(&elem_cheval) == -1){
                perror("elem_sem_deverrouiller");
                exit(-1);
            }

            /* Pose sémaphore sur cell arrive */
            P(semid_piste, arrivee);

            /* Nouvelle case du cheval */
            piste_cell_affecter(piste, arrivee, cell_cheval);

            /* Leve sémaphore sur cell arrive */
            V(semid_piste, arrivee);


#ifdef _DEBUG_
            printf("Deplacement du cheval \"%c\" de %d a %d\n",marque, depart, arrivee );
#endif
        }
        /* Affichage de la piste  */
        piste_afficher_lig( piste );

        depart = arrivee ;
    }

    printf( "Le cheval \"%c\" A FRANCHIT LA LIGNE D ARRIVEE\n" , marque );

    /*
     * Suppression du cheval de la liste
     */
    /* Pose semaphore piste */
    semop(semid_piste, &Op_P, 1);
    /* Delete du cheval arrive sur la piste */
    piste_cell_effacer(piste, depart);
    /* Leve semaphore piste */
    semop(semid_piste, &Op_V, 1);
    /* Pose semaphore liste */
    semop(semid_liste, &Op_P, 1);
    /* Suppression du cheval dans la liste */
    if(liste_elem_supprimer(liste, pos_cheval) != 0){
        perror("liste_elem_supprimer");
        exit(-1);
    }
    /* Leve semaphore liste */
    semop(semid_liste, &Op_V, 1);
    /* Destruction du cheval */
    if(elem_sem_detruire(&elem_cheval) == -1){
        perror("elem_sem_detruire");
        exit(-1);
    }

    exit(0);
}
