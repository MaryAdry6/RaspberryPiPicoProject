window.addEventListener("load", initpage);

// Înlocuiește cu IP-ul pe care îl primește Pico în consolă
const PICO_IP = "http://192.168.0.231"; 

function initpage() {
    const butoaneEfecte = document.querySelectorAll('.efect button');
    butoaneEfecte.forEach(buton => {
        buton.addEventListener('click', function() {
            this.classList.toggle('activat');
            
            // Determinăm ce LED trebuie controlat în funcție de ID-ul butonului
            let ledId = "";
            if(this.id === "distors2") ledId = "red";
            if(this.id === "reverb2") ledId = "blue";
            if(this.id === "delay2") ledId = "yellow";

            // Trimitem cererea către Pico
            fetch(`${PICO_IP}/toggle?led=${ledId}`)
                .catch(err => console.log("Pico nu a răspuns: ", err));
        });
    });
}