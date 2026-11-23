import React, { useEffect, useRef } from "react";

// Orthographic desk diagram. Dimensions below are drawing units.
export default function DeskModel() {
  const ref = useRef(null);
  useEffect(() => {
    const canvas = ref.current;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    const scale = 2;
    canvas.width = 560 * scale;
    canvas.height = 250 * scale;
    ctx.scale(scale, scale);
    ctx.lineJoin = "round";
    const project = ([x,y,z]) => [259 + x * 1.25 - y * 1.22, 140 + x * .42 + y * .46 - z * 1.08];
    function path(points, color, fill, width=1) {
      ctx.beginPath();
      points.map(project).forEach(([x,y],i) => i ? ctx.lineTo(x,y) : ctx.moveTo(x,y));
      if(fill){ctx.closePath();ctx.fillStyle=fill;ctx.fill()}
      ctx.strokeStyle=color;ctx.lineWidth=width;ctx.stroke();
    }
    function block(x,y,z,w,d,h,faces) {
      const [a,b,c]=faces, line="#75a79d";
      path([[x,y+d,z],[x+w,y+d,z],[x+w,y+d,z+h],[x,y+d,z+h]],line,b);
      path([[x+w,y,z],[x+w,y+d,z],[x+w,y+d,z+h],[x+w,y,z+h]],line,c);
      path([[x,y,z+h],[x+w,y,z+h],[x+w,y+d,z+h],[x,y+d,z+h]],line,a);
    }
    const metal=["#29413e","#172b29","#203431"],top=["#334941","#21342f","#293d37"];
    const bright="#b6eba9";
    for(const x of [-78,71])for(const y of [-27,27])block(x,y,-48,4,4,48,metal);
    block(-82,-31,0,163,70,4,top);
    block(-21,-11,4,39,14,2,metal); block(-6,-9,6,8,5,21,metal);
    block(-51,-13,21,96,3,55,metal);
    path([[-48,-9.8,25],[42,-9.8,25],[42,-9.8,73],[-48,-9.8,73]],"#82b4a9","#102622");
    path([[-40,-9,47],[-24,-9,47],[-19,-9,52],[-14,-9,40],[-8,-9,62],[-2,-9,43],[5,-9,49],[12,-9,47],[31,-9,47]],bright,null,1.7);
    block(-31,9,4,47,14,2,metal);
    for(let x=-28;x<14;x+=5)path([[x,11,6.2],[x,21,6.2]],"#78998c",null,.55);
    for(let y=12;y<22;y+=3)path([[-29,y,6.2],[14,y,6.2]],"#78998c",null,.55);
    block(27,12,4,8,11,3,metal);
    // Chair, seat column and five-point base.
    for(let i=0;i<5;i++){let a=i*2*Math.PI/5;path([[0,58,-32],[18*Math.cos(a),58+18*Math.sin(a),-43]],"#63867a",null,2.6)}
    block(-2,57,-33,4,4,17,metal);block(-16,44,-16,33,27,4,metal);
    block(-14,68,-11,30,4,26,metal);
    path([[51,-8,70],[73,-8,83],[91,-8,83]],bright,null);
    const [px,py]=project([51,-8,70]);ctx.fillStyle=bright;ctx.beginPath();ctx.arc(px,py,2.5,0,2*Math.PI);ctx.fill();
    const [tx,ty]=project([91,-8,83]);ctx.font="10px monospace";ctx.fillText("DISPLAY",tx+5,ty+3);
  }, []);
  return <canvas className="desk-model" ref={ref} role="img" aria-label="Technical illustration of the monitored workstation"/>;
}
