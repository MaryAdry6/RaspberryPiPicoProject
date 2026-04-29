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

    initKnobs();

}


function initKnobs() {
    const knobs = document.querySelectorAll('.knob-container');
    
    knobs.forEach(container => {
        const knob = container.querySelector('.knob');
        const ticksContainer = container.querySelector('.ticks');
        const hiddenInput = container.querySelector('input[type="hidden"]')
        const valueDisplay = container.querySelector('.knob-value'); 

        const minAngle = -135; // Start angle (bottom left)
        const maxAngle = 135;  // End angle (bottom right)
        const numTicks = 27;   // Total number of LED ticks
        const angleRange = maxAngle - minAngle;

        // Generate the HTML for the ticks dynamically
        const ticks = [];
        for(let i = 0; i < numTicks; i++) {
            let tick = document.createElement('div');
            tick.className = 'tick';
            let angle = minAngle + (i / (numTicks - 1)) * angleRange;
            // Push them outwards by 65px radius
            tick.style.transform = `rotate(${angle}deg) translateY(-65px)`; 
            ticksContainer.appendChild(tick);
            ticks.push({ el: tick, angle: angle });
        }

        // --- Mouse & Touch Drag Logic (Linear Vertical) ---
        let isDragging = false;
        let startY = 0;
        let startPercentage = 0;
        let currentPercentage = 0; // Track the current value

        // Function to update visuals based on 0.0 to 1.0 percentage
        function updateKnob(percentage) {
            percentage = Math.max(0, Math.min(1, percentage)); // Clamp between 0 and 1
            currentPercentage = percentage; // Save state here
            
            let currentAngle = minAngle + (percentage * angleRange);

            // Rotate the physical dark knob
            knob.style.transform = `rotate(${currentAngle}deg)`;

            // Light up the correct number of ticks
            ticks.forEach(t => {
                if (t.angle <= currentAngle) {
                    t.el.classList.add('active');
                } else {
                    t.el.classList.remove('active');
                }
            });

            let numericValue = Math.round(percentage * 10);

            // Update your hidden input with a scale from 0 to 10
            if(hiddenInput) {
                hiddenInput.value = Math.round(percentage * 10);
            }

            if(valueDisplay) {
                valueDisplay.textContent = numericValue;
            }
        }

        const startDrag = (e) => { 
            isDragging = true; 
            // Get the starting Y coordinate
            startY = e.clientY || (e.touches && e.touches[0].clientY);
            // Lock in the percentage at the moment of clicking
            startPercentage = currentPercentage; 
        };
        
        const stopDrag = () => { 
            isDragging = false; 
        };
        
        const onDrag = (e) => {
            if(!isDragging) return;

            let clientY = e.clientY || (e.touches && e.touches[0].clientY);
            if(clientY === undefined) return;

            // Calculate how many pixels the mouse moved vertically since clicking
            const deltaY = clientY - startY;
            
            // SENSITIVITY CONTROL: 
            // 200 means dragging the mouse 200 pixels covers the full 0% to 100% range.
            // Increase this number to make it LESS sensitive, decrease to make it MORE sensitive.
            const dragSensitivity = 200; 

            // Moving the mouse UP decreases the Y pixel value (negative delta), 
            // but we want moving UP to turn the knob UP (positive increase). 
            // So we invert the math with a minus sign.
            const percentageChange = -(deltaY / dragSensitivity);
            
            let newPercentage = startPercentage + percentageChange;
            updateKnob(newPercentage);
        };

        // Event Listeners for dragging
        knob.addEventListener('mousedown', startDrag);
        knob.addEventListener('touchstart', startDrag, {passive: false});

        window.addEventListener('mouseup', stopDrag);
        window.addEventListener('touchend', stopDrag);

        window.addEventListener('mousemove', onDrag);
        window.addEventListener('touchmove', (e) => {
            if(isDragging) e.preventDefault(); // Prevents page scrolling while turning knob
            onDrag(e);
        }, {passive: false});

        // Initialize knob to 0
        updateKnob(0);
    });
}